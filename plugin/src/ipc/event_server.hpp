#pragma once

#include "hyprcast/log/log_sink.hpp"
#include "hyprcast/os/unique_fd.hpp"
#include "os/file_lock.hpp"
#include "sock_file.hpp"
#include "wayland/wl_event_source.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>

namespace hyprcast {

class event_server {
public:
    using connected_cb =
        std::function<void()>; // A client connected and needs a full snapshot.
    using capture_cb = std::function<void(bool enabled)>;
    using disconnected_cb = std::function<void()>;

    // `log` must outlive the server.
    event_server(log_sink& log,
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

    static sock_paths init_sock_paths();
    static unique_fd create_socket();

    static int on_socket_readable(int sock_fd, std::uint32_t mask, void* data);
    static int on_client_event(int client_fd, std::uint32_t mask, void* data);

    void read_commands();
    void flush_messages();
    void update_wl_dispatch_event(std::uint32_t mask);

    log_sink* log_;
    connected_cb on_connected_;
    capture_cb on_capture_;
    disconnected_cb on_disconnected_;
    std::string command_buffer_;

    // Members are destroyed in reverse order: event sources are removed first,
    // then the client and listening descriptors close, the socket file is
    // unlinked, and the lock is released last. Keep this order when adding
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
