#pragma once

#include "hyprcast/log/log_sink.hpp"
#include "hyprcast/os/unique_fd.hpp"
#include "hyprcast/server/file_lock.hpp"
#include "hyprcast/server/sock_file.hpp"
#include "hyprcast/server/wl_event_source.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

struct wl_event_loop;

namespace hyprcast {

// The directory that holds the event socket and its lock for one compositor
// instance: <runtime_dir>/hyprcast/<instance_signature>. `runtime_dir` is the
// value of XDG_RUNTIME_DIR; throws if it is null or empty.
[[nodiscard]] std::filesystem::path
event_socket_dir(const char* runtime_dir, std::string_view instance_signature);

// Serves plugin events to one client at a time over a Unix socket
// (events.sock) in a directory it creates and guards with a lock file
// (server.lock). A new connection replaces the current client. Everything
// runs on the thread that dispatches `loop`.
class event_server {
public:
    using connected_cb =
        std::function<void()>; // A client connected and needs a full snapshot.
    using capture_cb = std::function<void(bool enabled)>;
    using disconnected_cb = std::function<void()>;

    // `loop` and `log` must outlive the server.
    event_server(wl_event_loop* loop,
                 const std::filesystem::path& sock_dir,
                 log_sink& log,
                 connected_cb on_connected,
                 capture_cb on_capture,
                 disconnected_cb on_disconnected);

    // `this` is registered with the Wayland event loop, so the object must
    // never move.
    event_server(const event_server&) = delete;
    event_server& operator=(const event_server&) = delete;
    event_server(event_server&&) = delete;
    event_server& operator=(event_server&&) = delete;
    ~event_server() = default;

    void queue_message(std::string message);
    void disconnect_client() noexcept;

    void shutdown() noexcept;

private:
    struct sock_paths {
        std::filesystem::path sock_dir;
        std::filesystem::path sock_file;
        std::filesystem::path lock_file;
    };

    static sock_paths init_sock_paths(const std::filesystem::path& sock_dir);
    static unique_fd create_socket();

    static int on_socket_readable(int sock_fd, std::uint32_t mask, void* data);
    static int on_client_event(int client_fd, std::uint32_t mask, void* data);

    void read_commands();
    void flush_messages();
    void update_wl_dispatch_event(std::uint32_t mask);

    wl_event_loop* loop_;
    log_sink* log_;
    connected_cb on_connected_;
    capture_cb on_capture_;
    disconnected_cb on_disconnected_;
    std::string command_buffer_;

    // Members are destroyed in reverse order, so reading upwards from the
    // event sources: the sources are removed while the descriptors they watch
    // are still open, the client descriptor closes, the socket file is
    // unlinked, the listening descriptor closes, and the lock is released
    // last. Holding the lock until the socket file is gone is what keeps this
    // server from unlinking a successor's socket. Keep this order when adding
    // members.
    sock_paths sock_paths_;
    file_lock sock_lock_;
    unique_fd sock_fd_;
    sock_file sock_file_;

    unique_fd client_fd_;

    unique_wl_event_source wl_socket_readable_;
    unique_wl_event_source wl_client_events_;

    std::deque<std::string> message_queue_;
    std::size_t message_offset_ = 0;
};

} // namespace hyprcast
