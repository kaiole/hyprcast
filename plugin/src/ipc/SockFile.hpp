#pragma once

#include <filesystem>

#include <unistd.h>

namespace Hyprcast {
    class CSockFile {
      public:
        explicit CSockFile(const std::filesystem::path& sockPath) : m_sockPath(sockPath) {};
        ~CSockFile() noexcept {
            if (m_bound) {
                ::unlink(m_sockPath.c_str());
            }
        }

        CSockFile(const CSockFile& other)            = delete;
        CSockFile& operator=(const CSockFile& other) = delete;
        CSockFile(CSockFile&& other)                 = delete;
        CSockFile& operator=(CSockFile&& other)      = delete;

        void       markBound() noexcept {
            m_bound = true;
        }

      private:
        std::filesystem::path m_sockPath;
        bool                  m_bound = false;
    };
}
