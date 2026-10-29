#pragma once

#include "hyprcast/os/UniqueFd.hpp"
#include "os/FileLock.hpp"
#include "SockFile.hpp"
#include "wayland/WlEventSource.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>

namespace Hyprcast {
    class CEventServer {
      public:
        using ConnectedCB    = std::function<void()>; // A client connected and needs a full snapshot.
        using CaptureCB      = std::function<void(bool enabled)>;
        using DisconnectedCB = std::function<void()>;

        CEventServer(ConnectedCB onConnected, CaptureCB onCapture, DisconnectedCB onDisconnected);

        // `this` is registered with the Wayland event loop, so the object must never move.
        CEventServer(const CEventServer&)            = delete;
        CEventServer& operator=(const CEventServer&) = delete;
        CEventServer(CEventServer&&)                 = delete;
        CEventServer& operator=(CEventServer&&)      = delete;

        void          queueMessage(std::string message);
        void          disconnectClient() noexcept;

        void          shutdown() noexcept;

      private:
        struct SSockPaths {
            std::filesystem::path sockDir;
            std::filesystem::path sockFile;
            std::filesystem::path lockFile;
        };

        static SSockPaths initSockPaths();
        static CUniqueFd  createSocket();

        static int        onSocketReadable(int sockFd, std::uint32_t mask, void* data);
        static int        onClientEvent(int clientFd, std::uint32_t mask, void* data);

        void              readCommands();
        void              flushMessages();
        void              updateWlDispatchEvent(std::uint32_t mask);

        ConnectedCB       m_onConnected;
        CaptureCB         m_onCapture;
        DisconnectedCB    m_onDisconnected;
        std::string       m_commandBuffer;

        // Members are destroyed in reverse order: event sources are removed first, then the
        // client and listening descriptors close, the socket file is unlinked, and the
        // lock is released last. Keep this order when adding members.
        SSockPaths              m_sockPaths;
        CFileLock               m_sockLock;
        CUniqueFd               m_sockFd;
        CSockFile               m_sockFile;

        CUniqueFd               m_clientFd;

        CWlEventSource          m_wlSocketReadable;
        CWlEventSource          m_wlClientEvents;

        std::deque<std::string> m_messageQueue;
        std::size_t             m_messageOffset = 0;
    };
}
