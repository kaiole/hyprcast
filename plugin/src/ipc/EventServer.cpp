#include "EventServer.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>

#include <Compositor.hpp>
#include <utility>
#include <wayland-server-core.h>

namespace Hyprcast {
    namespace {
        constexpr int              BACKLOG_SIZE       = 1;
        constexpr std::size_t      SOCK_PATH_CAPACITY = sizeof(::sockaddr_un{}.sun_path);
        constexpr std::string_view SOCK_DIR           = "hyprcast";
        constexpr std::string_view SOCK_FILE          = "events.sock";
        constexpr std::string_view LOCK_FILE          = "server.lock";
    }

    CEventServer::CEventServer() : m_sockPaths(initSockPaths()), m_sockLock(m_sockPaths.lockFile), m_sockFd(CUniqueFd{createSocket()}), m_sockFile(m_sockPaths.sockFile) {
        std::error_code errorCode;
        const auto      sockStatus = std::filesystem::symlink_status(m_sockPaths.sockFile, errorCode);
        if (errorCode && errorCode != std::errc::no_such_file_or_directory) {
            throw std::system_error(errorCode, "inspect socket pathname");
        }

        if (sockStatus.type() != std::filesystem::file_type::not_found) {
            if (!std::filesystem::is_socket(sockStatus)) {
                throw std::runtime_error("refusing to remove a non-socket entry at socket pathname");
            }

            if (::unlink(m_sockPaths.sockFile.c_str()) == -1 && errno != ENOENT) {
                throw std::system_error(errno, std::generic_category(), "unlink stale socket");
            }
        }

        ::sockaddr_un sockAddress{};
        sockAddress.sun_family = AF_UNIX;
        std::memcpy(sockAddress.sun_path, m_sockPaths.sockFile.c_str(), m_sockPaths.sockFile.native().size() + 1);

        int bindStatus = ::bind(m_sockFd.getFd(), reinterpret_cast<const ::sockaddr*>(&sockAddress), sizeof(sockAddress));
        if (bindStatus == -1) {
            throw std::system_error(errno, std::generic_category(), "bind");
        }

        m_sockFile.markBound();

        // TODO: if multiple clients is needed we need to increase BACKLOG_SIZE
        int listenStatus = ::listen(m_sockFd.getFd(), BACKLOG_SIZE);
        if (listenStatus == -1) {
            throw std::system_error(errno, std::generic_category(), "listen");
        }

        m_wlSocketReadable.reset(::wl_event_loop_add_fd(g_pCompositor->m_wlEventLoop, m_sockFd.getFd(), WL_EVENT_READABLE, &onSocketReadable, this));
        if (!m_wlSocketReadable) {
            throw std::runtime_error("failed to register IPC socket with Wayland event loop");
        }
    }

    void CEventServer::queueMessage(std::string message) {
        if (!m_clientFd.getFd()) {
            return;
        }

        constexpr int MAX_MESSAGES = 256;
        if (m_messageQueue.size() >= MAX_MESSAGES) {
            disconnectClient();
            return;
        }

        m_messageQueue.push_back(std::move(message));
        flushMessages();
    }

    CEventServer::SSockPaths CEventServer::initSockPaths() {
        const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
        if (runtimeDir == nullptr || runtimeDir[0] == '\0') {
            throw std::runtime_error("XDG_RUNTIME_DIR is unset");
        }

        const auto sockDir  = std::filesystem::path(runtimeDir) / SOCK_DIR / g_pCompositor->m_instanceSignature;
        const auto sockFile = sockDir / SOCK_FILE;

        if (sockFile.native().size() >= SOCK_PATH_CAPACITY) {
            throw std::runtime_error("unix socket path is too long");
        }

        std::error_code errorCode;
        std::filesystem::create_directories(sockDir, errorCode);
        if (errorCode) {
            throw std::system_error(errorCode, "failed to create socket directory");
        }

        return {.sockDir = sockDir, .sockFile = sockFile, .lockFile = sockDir / LOCK_FILE};
    }

    int CEventServer::createSocket() {
        int sockFd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (sockFd == -1) {
            throw std::system_error(errno, std::generic_category(), "socket");
        }

        return sockFd;
    }

    // wl_event_loop_add_fd requires a function ptr that returns an int
    int CEventServer::onSocketReadable(int sockFd, std::uint32_t mask, void* data) {
        auto* self = static_cast<CEventServer*>(data);

        int   clientFd = ::accept4(sockFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);

        // Client connection failure is not fatal to Hyprcast ∴
        // simply log the error and return Wayland event loop
        if (clientFd == -1) {
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
            //   A signal interrupted accept4() before it completed. If the connection
            //   is still pending, the event loop will call this function again.
            //
            // ECONNABORTED:
            //   The client disconnected before its pending connection could be
            //   accepted. There is nothing left to accept.
            //
            // These conditions can occur during normal operation, so do not log them
            // as errors.
            if (error == EAGAIN || error == EWOULDBLOCK || error == EINTR || error == ECONNABORTED) {
                return 0;
            }

            std::println(stderr, "[hyprcast] accept4 failed: {}; disabling new connections", std::strerror(error));
            self->m_wlSocketReadable.reset();

            return 0;
        }

        self->m_clientFd.reset(clientFd);
        self->m_wlClientWritable.reset(::wl_event_loop_add_fd(g_pCompositor->m_wlEventLoop, self->m_clientFd.getFd(), 0, &onClientWritable, self));

        return 0;
    }

    int CEventServer::onClientWritable(int clientFd, std::uint32_t mask, void* data) {
        auto* self = static_cast<CEventServer*>(data);

        if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
            self->disconnectClient();
            return 0;
        }

        if (mask & WL_EVENT_WRITABLE) {
            self->flushMessages();
        }

        return 0;
    }

    void CEventServer::flushMessages() {
        while (!m_messageQueue.empty()) {
            const auto& message = m_messageQueue.front();

            auto        bytesSent = ::send(m_clientFd.getFd(), message.data(), message.size(), MSG_NOSIGNAL);
            if (bytesSent == -1) {
                const int error = errno;

                if (error == EINTR) {
                    continue;
                }

                if (error == EAGAIN || error == EWOULDBLOCK) {
                    updateWlDispathEvent(WL_EVENT_WRITABLE);
                    return;
                }

                disconnectClient();
                return;
            }

            if (static_cast<std::size_t>(bytesSent) != message.size()) {
                disconnectClient();
                return;
            }

            m_messageQueue.pop_front();
        }

        updateWlDispathEvent(0);
    }

    void CEventServer::updateWlDispathEvent(std::uint32_t mask) {
        if (::wl_event_source_fd_update(m_wlClientWritable.get(), mask) == -1) {
            disconnectClient();
        }
    }

    void CEventServer::disconnectClient() {
        m_messageQueue.clear();
        m_wlClientWritable.reset();
        m_clientFd.reset();
    }
}
