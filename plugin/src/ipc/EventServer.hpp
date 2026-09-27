#pragma once

#include "os/FileLock.hpp"
#include "os/UniqueFd.hpp"
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
        using RegistrySnapshotCB = std::function<void()>;

        using CaptureCB = std::function<void(bool)>;
        explicit CEventServer(RegistrySnapshotCB callback, CaptureCB capture, RegistrySnapshotCB disconnected);
        ~CEventServer() = default;

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

        static SSockPaths       initSockPaths();
        static int              createSocket();

        static int              onSocketReadable(int sockFd, std::uint32_t mask, void* data);
        static int              onClientWritable(int clientFd, std::uint32_t mask, void* data);

        void                    readCommands();
        void                    flushMessages();
        void                    updateWlDispatchEvent(std::uint32_t mask);

        RegistrySnapshotCB      m_registrySnapshotCb;
        CaptureCB               m_captureCb;
        RegistrySnapshotCB      m_disconnectedCb;
        std::string             m_commandBuffer;

        SSockPaths              m_sockPaths;
        CFileLock               m_sockLock;
        CUniqueFd               m_sockFd;
        CSockFile               m_sockFile;

        CUniqueFd               m_clientFd;

        CWlEventSource          m_wlSocketReadable = nullptr;
        CWlEventSource          m_wlClientWritable = nullptr;

        std::deque<std::string> m_messageQueue;
        std::size_t             m_messageOffset = 0;
    };
}
