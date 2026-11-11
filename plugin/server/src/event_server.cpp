#include "hyprcast/server/event_server.hpp"

#include "hyprcast/log/log_sink.hpp"
#include "hyprcast/log/run_guarded.hpp"
#include "hyprcast/os/unique_fd.hpp"
#include "hyprcast/protocol/wire.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace hyprcast {
namespace {

constexpr int backlog_size = 1;
constexpr std::size_t sock_path_capacity = sizeof(::sockaddr_un {}.sun_path);
constexpr std::string_view runtime_subdir_name = "hyprcast";
constexpr std::string_view sock_file_name = "events.sock";
constexpr std::string_view lock_file_name = "server.lock";
// A longer unterminated buffer can never become a valid command.
constexpr std::size_t max_command_length =
    std::max(wire::command_enable.size(), wire::command_disable.size());

// A Wayland timer takes its delay as an int, and a delay of zero disarms it.
int to_timer_delay(std::chrono::milliseconds delay) {
    constexpr std::chrono::milliseconds::rep shortest = 1;
    constexpr std::chrono::milliseconds::rep longest =
        std::numeric_limits<int>::max();
    return static_cast<int>(std::clamp(delay.count(), shortest, longest));
}

} // namespace

std::filesystem::path event_socket_dir(const char* runtime_dir,
                                       std::string_view instance_signature) {
    if (runtime_dir == nullptr || runtime_dir[0] == '\0') {
        throw std::runtime_error("XDG_RUNTIME_DIR is unset");
    }

    return std::filesystem::path(runtime_dir) / runtime_subdir_name
        / instance_signature;
}

event_server::event_server(wl_event_loop* loop,
                           const std::filesystem::path& sock_dir,
                           log_sink& log,
                           connected_cb on_connected,
                           capture_cb on_capture,
                           disconnected_cb on_disconnected,
                           std::chrono::milliseconds accept_retry_delay)
    : loop_(loop),
      log_(&log),
      on_connected_(std::move(on_connected)),
      on_capture_(std::move(on_capture)),
      on_disconnected_(std::move(on_disconnected)),
      accept_retry_delay_ms_(to_timer_delay(accept_retry_delay)),
      sock_paths_(init_sock_paths(sock_dir)),
      sock_lock_(sock_paths_.lock_file),
      sock_fd_(create_socket()),
      sock_file_(sock_paths_.sock_file) {
    std::error_code error_code;
    const auto sock_status =
        std::filesystem::symlink_status(sock_paths_.sock_file, error_code);
    if (error_code && error_code != std::errc::no_such_file_or_directory) {
        throw std::system_error(error_code,
                                "Cannot inspect IPC socket '"
                                    + sock_paths_.sock_file.string() + "'");
    }

    if (sock_status.type() != std::filesystem::file_type::not_found) {
        if (!std::filesystem::is_socket(sock_status)) {
            throw std::runtime_error("Refusing to remove non-socket entry '"
                                     + sock_paths_.sock_file.string() + "'");
        }

        if (::unlink(sock_paths_.sock_file.c_str()) == -1 && errno != ENOENT) {
            throw std::system_error(errno,
                                    std::generic_category(),
                                    "Cannot remove stale IPC socket '"
                                        + sock_paths_.sock_file.string() + "'");
        }
    }

    ::sockaddr_un sock_address {};
    sock_address.sun_family = AF_UNIX;
    std::memcpy(std::data(sock_address.sun_path),
                sock_paths_.sock_file.c_str(),
                sock_paths_.sock_file.native().size() + 1);

    const int bind_status =
        ::bind(sock_fd_.get(),
               // bind(2) takes every address family as a sockaddr*.
               // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
               reinterpret_cast<const ::sockaddr*>(&sock_address),
               sizeof(sock_address));
    if (bind_status == -1) {
        throw std::system_error(errno,
                                std::generic_category(),
                                "Cannot bind IPC socket '"
                                    + sock_paths_.sock_file.string() + "'");
    }

    sock_file_.mark_bound();

    const int listen_status = ::listen(sock_fd_.get(), backlog_size);
    if (listen_status == -1) {
        throw std::system_error(errno,
                                std::generic_category(),
                                "Cannot listen on IPC socket '"
                                    + sock_paths_.sock_file.string() + "'");
    }

    wl_socket_readable_.reset(::wl_event_loop_add_fd(loop_,
                                                     sock_fd_.get(),
                                                     WL_EVENT_READABLE,
                                                     &on_socket_readable,
                                                     this));
    if (!wl_socket_readable_) {
        throw std::runtime_error(
            "Cannot register IPC socket with Wayland event loop");
    }
    wl_accept_retry_.reset(
        ::wl_event_loop_add_timer(loop_, &on_accept_retry, this));
    if (!wl_accept_retry_) {
        throw std::runtime_error(
            "Cannot register IPC retry timer with Wayland event loop");
    }
    log_message(*log_,
                log_level::trace,
                "Listening on IPC socket '{}'",
                sock_paths_.sock_file.string());
}

void event_server::queue_message(std::string message) {
    if (!client_fd_.valid()) {
        return;
    }

    message.push_back('\n');
    if (message.size() > max_queued_bytes - queued_bytes_) {
        log_message(*log_,
                    log_level::warning,
                    "Client disconnected: outgoing queue would exceed {} bytes",
                    max_queued_bytes);
        disconnect_client();
        return;
    }

    queued_bytes_ += message.size();
    message_queue_.push_back(std::move(message));
    flush_messages();
}

void event_server::disconnect_client() noexcept {
    const bool had_client = client_fd_.valid();
    if (had_client) {
        log_message(*log_, log_level::trace, "Client disconnected");
    }

    command_buffer_.clear();
    message_offset_ = 0;
    queued_bytes_ = 0;
    message_queue_.clear();
    wl_client_events_.reset();
    client_writable_watched_ = false;
    client_fd_.reset();
    if (had_client) {
        run_guarded(*log_,
                    "Cannot pause disconnected capture",
                    on_disconnected_,
                    [] noexcept {});
    }
}

void event_server::shutdown() noexcept {
    disconnect_client();
    stop_accepting();
    sock_fd_.reset();
}

event_server::sock_paths
event_server::init_sock_paths(const std::filesystem::path& sock_dir) {
    const auto sock_file = sock_dir / sock_file_name;

    if (sock_file.native().size() >= sock_path_capacity) {
        throw std::runtime_error("IPC socket path exceeds Unix socket limit: '"
                                 + sock_file.string() + "'");
    }

    std::error_code error_code;
    std::filesystem::create_directories(sock_dir, error_code);
    if (error_code) {
        throw std::system_error(error_code,
                                "Cannot create socket directory '"
                                    + sock_dir.string() + "'");
    }
    restrict_to_owner(sock_dir);

    return {
        .sock_dir = sock_dir,
        .sock_file = sock_file,
        .lock_file = sock_dir / lock_file_name,
    };
}

void event_server::restrict_to_owner(const std::filesystem::path& sock_dir) {
    // Opened without following a symlink, so the checks and the chmod apply
    // to the directory itself and not to wherever a link would lead.
    const unique_fd dir_fd {::open(sock_dir.c_str(),
                                   O_RDONLY | O_DIRECTORY | O_NOFOLLOW
                                       | O_CLOEXEC)};
    if (!dir_fd) {
        throw std::system_error(errno,
                                std::generic_category(),
                                "Cannot open socket directory '"
                                    + sock_dir.string() + "'");
    }
    struct ::stat info {};
    if (::fstat(dir_fd.get(), &info) == -1) {
        throw std::system_error(errno,
                                std::generic_category(),
                                "Cannot inspect socket directory '"
                                    + sock_dir.string() + "'");
    }
    if (info.st_uid != ::geteuid()) {
        throw std::runtime_error("Refusing socket directory owned by another "
                                 "user: '"
                                 + sock_dir.string() + "'");
    }
    // Every keystroke passes through this directory: no one else may reach
    // it, whatever the runtime directory above it allows.
    constexpr ::mode_t owner_only = S_IRWXU;
    if ((info.st_mode & 07777u) != owner_only
        && ::fchmod(dir_fd.get(), owner_only) == -1) {
        throw std::system_error(errno,
                                std::generic_category(),
                                "Cannot restrict socket directory '"
                                    + sock_dir.string() + "'");
    }
}

unique_fd event_server::create_socket() {
    unique_fd sock_fd {
        ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)
    };
    if (!sock_fd) {
        throw std::system_error(errno,
                                std::generic_category(),
                                "Cannot create IPC socket");
    }

    return sock_fd;
}

// wl_event_loop_add_fd requires a function ptr that returns an int
int event_server::on_socket_readable(int sock_fd,
                                     std::uint32_t /*mask*/,
                                     void* data) {
    auto* self = static_cast<event_server*>(data);

    unique_fd client_fd {
        ::accept4(sock_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)
    };

    if (!client_fd) {
        const int error = errno;

        // These are expected, recoverable accept4() failures:
        //
        // EAGAIN / EWOULDBLOCK:
        //   There is currently no pending connection for accept4() to return.
        //   Since the listening socket is non-blocking, accept4() returns -1
        //   instead of blocking the compositor thread. Return to the Wayland
        //   event loop; it will call this function again when the listening
        //   socket becomes readable.
        //
        // EINTR:
        //   A signal interrupted accept4() before it completed. If the
        //   connection is still pending, the event loop will call this function
        //   again.
        //
        // ECONNABORTED:
        //   The client disconnected before its pending connection could be
        //   accepted. There is nothing left to accept.
        //
        // These conditions can occur during normal operation, so do not log
        // them as errors.
        if (error == EAGAIN || error == EWOULDBLOCK || error == EINTR
            || error == ECONNABORTED) {
            return 0;
        }

        self->pause_accepting(error);
        return 0;
    }

    if (self->accept_failing_) {
        self->accept_failing_ = false;
        log_message(*self->log_,
                    log_level::warning,
                    "Accepting IPC connections again");
    }

    if (self->client_fd_.valid()) {
        log_message(*self->log_,
                    log_level::trace,
                    "Replacing existing client with new connection");
    }
    self->disconnect_client();

    self->client_fd_ = std::move(client_fd);
    log_message(*self->log_, log_level::trace, "Client connected");

    self->wl_client_events_.reset(::wl_event_loop_add_fd(self->loop_,
                                                         self->client_fd_.get(),
                                                         WL_EVENT_READABLE,
                                                         &on_client_event,
                                                         self));
    if (!self->wl_client_events_) {
        log_error(*self->log_,
                  "Cannot register client with Wayland event loop; "
                  "disconnecting client");
        self->disconnect_client();
        return 0;
    }

    run_guarded(
        *self->log_,
        "Cannot prepare initial keyboard snapshot; disconnecting client",
        self->on_connected_,
        [self] noexcept { self->disconnect_client(); });

    return 0;
}

// Any other accept4() failure, such as the process being out of descriptors,
// leaves the connection pending and the listening socket readable. Returning
// to the event loop as it is would call straight back here, for as long as
// the cause lasts. So the socket goes unwatched until a timer has run out.
void event_server::pause_accepting(int error) {
    if (!accept_failing_) {
        accept_failing_ = true;
        log_error(*log_,
                  "Cannot accept IPC connection: {}; trying again every {} ms",
                  std::strerror(error),
                  accept_retry_delay_ms_);
    }

    if (::wl_event_source_fd_update(wl_socket_readable_.get(), 0) == -1
        || ::wl_event_source_timer_update(wl_accept_retry_.get(),
                                          accept_retry_delay_ms_)
            == -1) {
        give_up_accepting();
    }
}

int event_server::on_accept_retry(void* data) {
    auto* self = static_cast<event_server*>(data);

    // Watching the socket again is the retry: a connection that is still
    // pending makes it readable at once.
    if (::wl_event_source_fd_update(self->wl_socket_readable_.get(),
                                    WL_EVENT_READABLE)
        == -1) {
        self->give_up_accepting();
    }

    return 0;
}

// Without a way to come back to the socket later, the choice is between
// never accepting again and being called for it without pause.
void event_server::give_up_accepting() noexcept {
    stop_accepting();
    log_error(*log_,
              "Cannot schedule another attempt to accept IPC connections; "
              "disabling new connections until plugin reload");
    log_->notify_failure("IPC listener stopped. Unload and reload hyprcast; "
                         "see Hyprland logs for details.");
}

void event_server::stop_accepting() noexcept {
    wl_accept_retry_.reset();
    wl_socket_readable_.reset();
}

int event_server::on_client_event(int /*client_fd*/,
                                  std::uint32_t mask,
                                  void* data) {
    auto* self = static_cast<event_server*>(data);

    if (mask & static_cast<std::uint32_t>(WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        if (mask & WL_EVENT_ERROR) {
            log_error(
                *self->log_,
                "Wayland reported a client socket error; disconnecting client");
        }

        self->disconnect_client();
        return 0;
    }

    if (mask & WL_EVENT_READABLE) {
        run_guarded(
            *self->log_,
            "Cannot process capture command",
            [self] { self->read_commands(); },
            [self] noexcept { self->disconnect_client(); });
    }
    if (!self->client_fd_.valid()) {
        return 0;
    }
    if (mask & WL_EVENT_WRITABLE) {
        self->flush_messages();
    }

    return 0;
}

void event_server::read_commands() {
    // Bound work per compositor dispatch as well as per command.
    std::array<char, 64> buffer {};
    const auto size = ::recv(client_fd_.get(), buffer.data(), buffer.size(), 0);
    if (size < 0
        && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return;
    }
    if (size <= 0) {
        disconnect_client();
        return;
    }
    command_buffer_.append(buffer.data(), static_cast<std::size_t>(size));
    while (true) {
        const auto end = command_buffer_.find('\n');
        if (end == std::string::npos) {
            if (command_buffer_.size() > max_command_length) {
                disconnect_client();
            }
            return;
        }
        const std::string_view command {command_buffer_.data(), end};
        if (command != wire::command_enable
            && command != wire::command_disable) {
            disconnect_client();
            return;
        }
        const bool enable = command == wire::command_enable;
        command_buffer_.erase(0, end + 1);
        on_capture_(enable);
        if (!client_fd_.valid()) {
            return;
        }
    }
}

void event_server::flush_messages() {
    while (!message_queue_.empty()) {
        std::string_view message = message_queue_.front();

        const auto full_message_size = message.size();
        if (message_offset_ >= full_message_size) {
            log_error(*log_,
                      "Invalid outgoing message offset {} for {} bytes; "
                      "disconnecting client",
                      message_offset_,
                      full_message_size);
            disconnect_client();
            return;
        }

        message = message.substr(message_offset_);

        const auto bytes_sent = ::send(client_fd_.get(),
                                       message.data(),
                                       message.size(),
                                       MSG_NOSIGNAL);
        if (bytes_sent == -1) {
            const int error = errno;

            if (error == EINTR) {
                continue;
            }

            if (error == EAGAIN || error == EWOULDBLOCK) {
                watch_client_writable(true);
                return;
            }

            if (error != EPIPE && error != ECONNRESET) {
                log_error(*log_,
                          "Cannot send IPC message: {}; disconnecting client",
                          std::strerror(error));
            }
            disconnect_client();
            return;
        }

        message_offset_ += static_cast<std::size_t>(bytes_sent);
        queued_bytes_ -= static_cast<std::size_t>(bytes_sent);
        if (message_offset_ != full_message_size) {
            watch_client_writable(true);
            return;
        }

        message_queue_.pop_front();
        message_offset_ = 0;
    }

    watch_client_writable(false);
}

void event_server::watch_client_writable(bool watch) {
    // Nearly every flush ends the way the previous one did, so only a change
    // is worth the system call behind the update.
    if (watch == client_writable_watched_) {
        return;
    }

    const std::uint32_t mask =
        watch ? WL_EVENT_READABLE | WL_EVENT_WRITABLE : WL_EVENT_READABLE;
    if (::wl_event_source_fd_update(wl_client_events_.get(), mask) == -1) {
        log_error(
            *log_,
            "Cannot update client Wayland event source; disconnecting client");
        disconnect_client();
        return;
    }
    client_writable_watched_ = watch;
}

} // namespace hyprcast
