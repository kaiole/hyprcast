#pragma once

#include "BoundSockFile.hpp"
#include "os/FileLock.hpp"
#include "os/UniqueFd.hpp"
#include "wayland/WlEventSource.hpp"

#include <cstdint>
#include <filesystem>
#include <sys/un.h>

namespace Hyprcast {
    class CSocketServer {
      public:
        CSocketServer();
        ~CSocketServer() = default;

        CSocketServer(const CSocketServer&)            = delete;
        CSocketServer& operator=(const CSocketServer&) = delete;
        CSocketServer(CSocketServer&&)                 = delete;
        CSocketServer& operator=(CSocketServer&&)      = delete;

      private:
        struct SSockInfo {
            std::filesystem::path dir;
            std::filesystem::path path;
            std::filesystem::path lock;
        };

        static int       createSocket();
        static SSockInfo initSockInfo();
        static int       onSocketReadable(int sockFd, uint32_t mask, void* data);

        SSockInfo        m_sockInfo;
        CFileLock        m_sockLock;
        CUniqueFd        m_sockFd;
        CBoundSockFile   m_boundSockFile;
        CUniqueFd        m_clientFd;

        CWlEventSource   m_eventSource = nullptr;
    };
}
