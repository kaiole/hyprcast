#pragma once

#include "hyprcast/core/UniqueFd.hpp"

#include <string>

namespace Hyprcast {
    class CSocketLock {
      public:
        explicit CSocketLock(const std::string& lockDir);
        ~CSocketLock() noexcept = default;

        CSocketLock(const CSocketLock&)            = delete;
        CSocketLock& operator=(const CSocketLock&) = delete;
        CSocketLock(CSocketLock&&)                 = delete;
        CSocketLock& operator=(CSocketLock&&)      = delete;

      private:
        CUniqueFd m_lockFd;
    };
}
