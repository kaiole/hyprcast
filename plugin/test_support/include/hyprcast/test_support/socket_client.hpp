#pragma once

#include "hyprcast/os/unique_fd.hpp"

#include <sys/socket.h>
#include <sys/un.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace hyprcast::test_support {

[[nodiscard]] inline ::sockaddr_un
unix_address(const std::filesystem::path& sock_path) {
    ::sockaddr_un address {};
    address.sun_family = AF_UNIX;
    if (sock_path.native().size() >= sizeof(address.sun_path)) {
        throw std::runtime_error("Socket path is too long: '"
                                 + sock_path.string() + "'");
    }
    std::memcpy(std::data(address.sun_path),
                sock_path.c_str(),
                sock_path.native().size() + 1);
    return address;
}

[[nodiscard]] inline const ::sockaddr*
as_sockaddr(const ::sockaddr_un& address) noexcept {
    // bind(2) and connect(2) take every address family as a sockaddr*.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<const ::sockaddr*>(&address);
}

// Binds a Unix socket at `sock_path` without listening on it. Closing the
// returned descriptor leaves the socket file behind, the way a crashed server
// would.
[[nodiscard]] inline unique_fd
bind_unix_socket(const std::filesystem::path& sock_path) {
    unique_fd sock_fd {::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (!sock_fd) {
        throw std::system_error(errno,
                                std::generic_category(),
                                "Cannot create a socket");
    }
    const auto address = unix_address(sock_path);
    if (::bind(sock_fd.get(), as_sockaddr(address), sizeof(address)) == -1) {
        throw std::system_error(errno,
                                std::generic_category(),
                                "Cannot bind '" + sock_path.string() + "'");
    }
    return sock_fd;
}

// The other end of the event socket: what the overlay is to the plugin. It
// never blocks, so a test drives the server's event loop and looks at what
// has arrived in between.
class socket_client {
public:
    // Throws std::system_error if nothing accepts connections on `sock_path`.
    explicit socket_client(const std::filesystem::path& sock_path)
        : sock_fd_(::socket(AF_UNIX,
                            SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                            0)) {
        if (!sock_fd_) {
            throw std::system_error(errno,
                                    std::generic_category(),
                                    "Cannot create a socket");
        }
        const auto address = unix_address(sock_path);
        if (::connect(sock_fd_.get(), as_sockaddr(address), sizeof(address))
            == -1) {
            throw std::system_error(errno,
                                    std::generic_category(),
                                    "Cannot connect to '" + sock_path.string()
                                        + "'");
        }
    }

    // Writes all of `data`; tests only send a few bytes at a time.
    void send(std::string_view data) {
        while (!data.empty()) {
            const auto sent =
                ::send(sock_fd_.get(), data.data(), data.size(), MSG_NOSIGNAL);
            if (sent == -1) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::system_error(errno,
                                        std::generic_category(),
                                        "Cannot send to the server");
            }
            data.remove_prefix(static_cast<std::size_t>(sent));
        }
    }

    // Closes this end, as an exiting overlay does.
    void close() noexcept { sock_fd_.reset(); }

    // Everything received so far.
    [[nodiscard]] const std::string& received() {
        read_available();
        return received_;
    }

    // The complete lines received so far, without their terminators.
    [[nodiscard]] std::vector<std::string> lines() {
        read_available();

        std::vector<std::string> result;
        std::size_t begin = 0;
        while (true) {
            const auto end = received_.find('\n', begin);
            if (end == std::string::npos) {
                return result;
            }
            result.push_back(received_.substr(begin, end - begin));
            begin = end + 1;
        }
    }

    // Whether the server has closed the connection. Data it sent before that
    // is still returned by received() and lines().
    [[nodiscard]] bool closed() {
        read_available();
        return closed_;
    }

private:
    void read_available() {
        std::array<char, 65536> buffer {};
        while (sock_fd_.valid() && !closed_) {
            const auto size =
                ::recv(sock_fd_.get(), buffer.data(), buffer.size(), 0);
            if (size > 0) {
                received_.append(buffer.data(), static_cast<std::size_t>(size));
                continue;
            }
            if (size == 0 || errno == ECONNRESET) {
                closed_ = true;
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            throw std::system_error(errno,
                                    std::generic_category(),
                                    "Cannot receive from the server");
        }
    }

    unique_fd sock_fd_;
    std::string received_;
    bool closed_ = false;
};

} // namespace hyprcast::test_support
