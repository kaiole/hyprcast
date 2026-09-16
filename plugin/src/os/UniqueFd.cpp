#include "UniqueFd.hpp"

#include <unistd.h>

namespace Hyprcast {
    CUniqueFd::~CUniqueFd() noexcept {
        reset();
    }

    void CUniqueFd::reset(int fd) noexcept {
        if (m_fd == fd) {
            return;
        }

        if (m_fd != -1) {
            ::close(m_fd);
        }

        m_fd = fd;
    }
}
