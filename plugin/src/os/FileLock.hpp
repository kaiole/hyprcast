#pragma once

#include "UniqueFd.hpp"

#include <filesystem>

namespace Hyprcast {
    class CFileLock {
      public:
        explicit CFileLock(const std::filesystem::path& lockPath);
        ~CFileLock() noexcept = default;

        CFileLock(const CFileLock&)            = delete;
        CFileLock& operator=(const CFileLock&) = delete;
        CFileLock(CFileLock&&)                 = delete;
        CFileLock& operator=(CFileLock&&)      = delete;

      private:
        CUniqueFd m_lockFd;
    };
}
