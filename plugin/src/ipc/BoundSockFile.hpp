#pragma once

#include <filesystem>

#include <unistd.h>

namespace Hyprcast {
    class CBoundSockFile {
      public:
        explicit CBoundSockFile(const std::filesystem::path& sockPath) : m_sockPath(sockPath) {};
        ~CBoundSockFile() noexcept {
            if (m_bound) {
                ::unlink(m_sockPath.c_str());
            }
        }

        CBoundSockFile(const CBoundSockFile& other)            = delete;
        CBoundSockFile& operator=(const CBoundSockFile& other) = delete;
        CBoundSockFile(CBoundSockFile&& other)                 = delete;
        CBoundSockFile& operator=(CBoundSockFile&& other)      = delete;

        void            markBound() noexcept {
            m_bound = true;
        }

      private:
        std::filesystem::path m_sockPath;
        bool                  m_bound = false;
    };
}
