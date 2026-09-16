#include "SocketServer.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <print>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>

#include <Compositor.hpp>
#include <wayland-server-core.h>

namespace Hyprcast {
    namespace {
        constexpr int              BACKLOG_SIZE       = 1;
        constexpr std::size_t      SOCK_PATH_CAPACITY = sizeof(::sockaddr_un{}.sun_path);
        constexpr std::string_view SOCK_DIR           = "hyprcast";
        constexpr std::string_view SOCK_FILE          = "events.sock";
        constexpr std::string_view SOCK_LOCK_FILE     = "server.lock";
    }

    CSocketServer::SSockInfo CSocketServer::initSockInfo() {
        const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
        if (runtimeDir == nullptr || runtimeDir[0] == '\0') {
            throw std::runtime_error("XDG_RUNTIME_DIR is unset");
        }

        const auto sockDir  = std::filesystem::path(runtimeDir) / SOCK_DIR / g_pCompositor->m_instanceSignature;
        const auto sockPath = sockDir / SOCK_FILE;

        if (sockPath.native().size() >= SOCK_PATH_CAPACITY) {
            throw std::runtime_error("unix socket path is too long");
        }

        std::error_code errorCode;
        std::filesystem::create_directories(sockDir, errorCode);
        if (errorCode) {
            throw std::system_error(errorCode, "failed to create socket directory");
        }

        return {.dir = sockDir, .path = sockPath, .lock = sockDir / SOCK_LOCK_FILE};
    }

    int CSocketServer::createSocket() {
        int sockFd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (sockFd == -1) {
            throw std::system_error(errno, std::generic_category(), "socket");
        }

        return sockFd;
    }

    CSocketServer::CSocketServer() : m_sockInfo(initSockInfo()), m_sockLock(m_sockInfo.lock), m_sockFd(CUniqueFd{createSocket()}), m_boundSockFile(m_sockInfo.path), m_clientFd() {
        std::error_code errorCode;
        const auto      sockStatus = std::filesystem::symlink_status(m_sockInfo.path, errorCode);
        if (errorCode && errorCode != std::errc::no_such_file_or_directory) {
            throw std::system_error(errorCode, "inspect socket pathname");
        }

        if (sockStatus.type() != std::filesystem::file_type::not_found) {
            if (!std::filesystem::is_socket(sockStatus)) {
                throw std::runtime_error("refusing to remove a non-socket entry at socket pathname");
            }

            if (::unlink(m_sockInfo.path.c_str()) == -1 && errno != ENOENT) {
                throw std::system_error(errno, std::generic_category(), "unlink stale socket");
            }
        }

        ::sockaddr_un sockAddress{};
        sockAddress.sun_family = AF_UNIX;
        std::memcpy(sockAddress.sun_path, m_sockInfo.path.c_str(), m_sockInfo.path.native().size() + 1);

        int bindStatus = ::bind(m_sockFd.getFd(), reinterpret_cast<const ::sockaddr*>(&sockAddress), sizeof(sockAddress));
        if (bindStatus == -1) {
            throw std::system_error(errno, std::generic_category(), "bind");
        }

        m_boundSockFile.markBound();

        int listenStatus = ::listen(m_sockFd.getFd(), BACKLOG_SIZE);
        if (listenStatus == -1) {
            throw std::system_error(errno, std::generic_category(), "listen");
        }

        m_eventSource.reset(::wl_event_loop_add_fd(g_pCompositor->m_wlEventLoop, m_sockFd.getFd(), WL_EVENT_READABLE, &onSocketReadable, this));
        if (!m_eventSource) {
            throw std::runtime_error("failed to register IPC socket with Wayland event loop");
        }
    }

    int CSocketServer::onSocketReadable(int sockFd, uint32_t mask, void* data) {
        // TODO: Is there use in multiple client connections? If not close client id if 1 client already connected.
        int clientFd = ::accept4(sockFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (clientFd == -1) {
            const int error = errno;

            if (error == EAGAIN || error == EWOULDBLOCK || error == EINTR || error == ECONNABORTED) {
                return 0;
            }

            std::println(stderr, "[hyprcast] accept4 failed: {}; disabling new connections", std::strerror(error));

            auto* self = static_cast<CSocketServer*>(data);
            self->m_eventSource.reset();
            return 0;
        }

        auto* self = static_cast<CSocketServer*>(data);
        self->m_clientFd.reset(clientFd);

        // TODO: stream data out
        return 0;
    }
}
