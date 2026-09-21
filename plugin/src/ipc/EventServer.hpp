#pragma once

#include "SockFile.hpp"
#include "os/FileLock.hpp"
#include "os/UniqueFd.hpp"
#include "wayland/WlEventSource.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <sys/un.h>

namespace Hyprcast {
    class CEventServer {
      public:
        CEventServer();
        ~CEventServer() = default;

        CEventServer(const CEventServer&)            = delete;
        CEventServer& operator=(const CEventServer&) = delete;
        CEventServer(CEventServer&&)                 = delete;
        CEventServer& operator=(CEventServer&&)      = delete;

        void          queueMessage(std::string message);

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

        void                    flushMessages();
        void                    updateWlDispathEvent(std::uint32_t mask);
        void                    disconnectClient();

        SSockPaths              m_sockPaths;
        CFileLock               m_sockLock;
        CUniqueFd               m_sockFd;
        CSockFile               m_sockFile;

        CUniqueFd               m_clientFd;

        CWlEventSource          m_wlSocketReadable = nullptr;
        CWlEventSource          m_wlClientWritable = nullptr;

        std::deque<std::string> m_messageQueue;
    };
}
