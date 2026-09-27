#include "EventServer.hpp"
#include "CallbackBoundary.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

    CEventServer::CEventServer(RegistrySnapshotCB callback, CaptureCB capture, RegistrySnapshotCB disconnected) :
        m_registrySnapshotCb(std::move(callback)), m_captureCb(std::move(capture)), m_disconnectedCb(std::move(disconnected)), m_sockPaths(initSockPaths()),
        m_sockLock(m_sockPaths.lockFile), m_sockFd(CUniqueFd{createSocket()}), m_sockFile(m_sockPaths.sockFile) {
        std::error_code errorCode;
        const auto      sockStatus = std::filesystem::symlink_status(m_sockPaths.sockFile, errorCode);
        if (errorCode && errorCode != std::errc::no_such_file_or_directory) {
            throw std::system_error(errorCode, "Cannot inspect IPC socket '" + m_sockPaths.sockFile.string() + "'");
        }

        if (sockStatus.type() != std::filesystem::file_type::not_found) {
            if (!std::filesystem::is_socket(sockStatus)) {
                throw std::runtime_error("Refusing to remove non-socket entry '" + m_sockPaths.sockFile.string() + "'");
            }

            if (::unlink(m_sockPaths.sockFile.c_str()) == -1 && errno != ENOENT) {
                throw std::system_error(errno, std::generic_category(), "Cannot remove stale IPC socket '" + m_sockPaths.sockFile.string() + "'");
            }
        }

        ::sockaddr_un sockAddress{};
        sockAddress.sun_family = AF_UNIX;
        std::memcpy(sockAddress.sun_path, m_sockPaths.sockFile.c_str(), m_sockPaths.sockFile.native().size() + 1);

        int bindStatus = ::bind(m_sockFd.getFd(), reinterpret_cast<const ::sockaddr*>(&sockAddress), sizeof(sockAddress));
        if (bindStatus == -1) {
            throw std::system_error(errno, std::generic_category(), "Cannot bind IPC socket '" + m_sockPaths.sockFile.string() + "'");
        }

        m_sockFile.markBound();

        // TODO: if multiple clients is needed we need to increase BACKLOG_SIZE
        int listenStatus = ::listen(m_sockFd.getFd(), BACKLOG_SIZE);
        if (listenStatus == -1) {
            throw std::system_error(errno, std::generic_category(), "Cannot listen on IPC socket '" + m_sockPaths.sockFile.string() + "'");
        }

        m_wlSocketReadable.reset(::wl_event_loop_add_fd(g_pCompositor->m_wlEventLoop, m_sockFd.getFd(), WL_EVENT_READABLE, &onSocketReadable, this));
        if (!m_wlSocketReadable) {
            throw std::runtime_error("Cannot register IPC socket with Wayland event loop");
        }
        logMessage(Log::TRACE, "Listening on IPC socket '{}'", m_sockPaths.sockFile.string());
    }

    void CEventServer::queueMessage(std::string message) {
        if (m_clientFd.getFd() == -1) {
            return;
        }

        constexpr std::size_t MAX_MESSAGES = 256;
        if (m_messageQueue.size() >= MAX_MESSAGES) {
            logMessage(Log::WARN, "Client disconnected: outgoing queue reached {} messages", MAX_MESSAGES);
            disconnectClient();
            return;
        }

        message.push_back('\n');
        m_messageQueue.push_back(std::move(message));
        flushMessages();
    }

    void CEventServer::disconnectClient() noexcept {
        if (m_clientFd.getFd() != -1) {
            logMessage(Log::TRACE, "Client disconnected");
        }

        const bool hadClient = m_clientFd.getFd() != -1;
        m_commandBuffer.clear();
        m_messageOffset = 0;
        m_messageQueue.clear();
        m_wlClientWritable.reset();
        m_clientFd.reset();
        if (hadClient) {
            runGuarded("Cannot pause disconnected capture", m_disconnectedCb, [] noexcept {});
        }
    }

    void CEventServer::shutdown() noexcept {
        disconnectClient();
        m_wlSocketReadable.reset();
        m_sockFd.reset();
    }

    CEventServer::SSockPaths CEventServer::initSockPaths() {
        const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
        if (runtimeDir == nullptr || runtimeDir[0] == '\0') {
            throw std::runtime_error("XDG_RUNTIME_DIR is unset");
        }

        const auto sockDir  = std::filesystem::path(runtimeDir) / SOCK_DIR / g_pCompositor->m_instanceSignature;
        const auto sockFile = sockDir / SOCK_FILE;

        if (sockFile.native().size() >= SOCK_PATH_CAPACITY) {
            throw std::runtime_error("IPC socket path exceeds Unix socket limit: '" + sockFile.string() + "'");
        }

        std::error_code errorCode;
        std::filesystem::create_directories(sockDir, errorCode);
        if (errorCode) {
            throw std::system_error(errorCode, "Cannot create socket directory '" + sockDir.string() + "'");
        }

        return {.sockDir = sockDir, .sockFile = sockFile, .lockFile = sockDir / LOCK_FILE};
    }

    int CEventServer::createSocket() {
        int sockFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (sockFd == -1) {
            throw std::system_error(errno, std::generic_category(), "Cannot create IPC socket");
        }

        return sockFd;
    }

    // wl_event_loop_add_fd requires a function ptr that returns an int
    int CEventServer::onSocketReadable(int sockFd, std::uint32_t mask, void* data) {
        auto* self = static_cast<CEventServer*>(data);

        int   clientFd = ::accept4(sockFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);

        // Ignore transient failures. Unexpected failures disable the listener
        // to avoid repeatedly waking the compositor on a broken socket.
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

            logError("Cannot accept IPC connection: {}; disabling new connections until plugin reload", std::strerror(error));
            notifyFailure("IPC listener stopped. Unload and reload hyprcast; see Hyprland logs for details.");
            self->m_wlSocketReadable.reset();

            return 0;
        }

        if (self->m_clientFd.getFd() != -1) {
            logMessage(Log::TRACE, "Replacing existing client with new connection");
        }
        self->disconnectClient();

        self->m_clientFd.reset(clientFd);
        logMessage(Log::TRACE, "Client connected");

        self->m_wlClientWritable.reset(::wl_event_loop_add_fd(g_pCompositor->m_wlEventLoop, self->m_clientFd.getFd(), WL_EVENT_READABLE, &onClientWritable, self));
        if (!self->m_wlClientWritable) {
            logError("Cannot register client with Wayland event loop; disconnecting client");
            self->disconnectClient();
            return 0;
        }

        runGuarded("Cannot prepare initial keyboard snapshot; disconnecting client", self->m_registrySnapshotCb, [self] noexcept { self->disconnectClient(); });

        return 0;
    }

    int CEventServer::onClientWritable(int clientFd, std::uint32_t mask, void* data) {
        auto* self = static_cast<CEventServer*>(data);

        if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
            if (mask & WL_EVENT_ERROR) {
                logError("Wayland reported a client socket error; disconnecting client");
            }

            self->disconnectClient();
            return 0;
        }

        if (mask & WL_EVENT_READABLE) {
            runGuarded("Cannot process capture command", [self] { self->readCommands(); }, [self] noexcept { self->disconnectClient(); });
        }
        if (self->m_clientFd.getFd() == -1) {
            return 0;
        }
        if (mask & WL_EVENT_WRITABLE) {
            self->flushMessages();
        }

        return 0;
    }

    void CEventServer::readCommands() {
        // Bound work per compositor dispatch as well as per command.
        char       buffer[64];
        const auto size = ::recv(m_clientFd.getFd(), buffer, sizeof(buffer), 0);
        if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return;
        }
        if (size <= 0) {
            disconnectClient();
            return;
        }
        m_commandBuffer.append(buffer, static_cast<std::size_t>(size));
        while (true) {
            const auto end = m_commandBuffer.find('\n');
            if (end == std::string::npos) {
                if (m_commandBuffer.size() > 7) {
                    disconnectClient();
                }
                return;
            }
            const auto command = m_commandBuffer.substr(0, end);
            m_commandBuffer.erase(0, end + 1);
            if (command != "enable" && command != "disable") {
                disconnectClient();
                return;
            }
            m_captureCb(command == "enable");
            if (m_clientFd.getFd() == -1) {
                return;
            }
        }
    }

    void CEventServer::flushMessages() {
        while (!m_messageQueue.empty()) {
            std::string_view message = m_messageQueue.front();

            const auto       fullMessageSize = message.size();
            if (m_messageOffset >= fullMessageSize) {
                logError("Invalid outgoing message offset {} for {} bytes; disconnecting client", m_messageOffset, fullMessageSize);
                disconnectClient();
                return;
            }

            message = message.substr(m_messageOffset);

            auto bytesSent = ::send(m_clientFd.getFd(), message.data(), message.size(), MSG_NOSIGNAL);
            if (bytesSent == -1) {
                const int error = errno;

                if (error == EINTR) {
                    continue;
                }

                if (error == EAGAIN || error == EWOULDBLOCK) {
                    updateWlDispatchEvent(WL_EVENT_WRITABLE);
                    return;
                }

                if (error != EPIPE && error != ECONNRESET) {
                    logError("Cannot send IPC message: {}; disconnecting client", std::strerror(error));
                }
                disconnectClient();
                return;
            }

            m_messageOffset += static_cast<std::size_t>(bytesSent);
            if (m_messageOffset != fullMessageSize) {
                updateWlDispatchEvent(WL_EVENT_WRITABLE);
                return;
            }

            m_messageQueue.pop_front();
            m_messageOffset = 0;
        }

        updateWlDispatchEvent(0);
    }

    void CEventServer::updateWlDispatchEvent(std::uint32_t mask) {
        if (::wl_event_source_fd_update(m_wlClientWritable.get(), mask | WL_EVENT_READABLE) == -1) {
            logError("Cannot update client Wayland event source; disconnecting client");
            disconnectClient();
        }
    }
}
