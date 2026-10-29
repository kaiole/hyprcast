#pragma once

#include "hyprcast/os/UniqueFd.hpp"

#include <filesystem>

namespace Hyprcast {
    // Holds an exclusive flock() for as long as the object lives. All special
    // members are implicit: CUniqueFd already makes this move-only (rule of zero).
    class CFileLock {
      public:
        explicit CFileLock(const std::filesystem::path& lockPath);

      private:
        CUniqueFd m_lockFd;
    };
}
