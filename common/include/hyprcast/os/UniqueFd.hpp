#pragma once

#include <unistd.h>

#include <utility>

namespace Hyprcast {
    // Move-only owner of a POSIX file descriptor, in the spirit of std::unique_ptr.
    // A negative descriptor means "empty".
    class CUniqueFd {
      public:
        CUniqueFd() noexcept = default;
        explicit CUniqueFd(int fd) noexcept : m_fd(fd) {}
        ~CUniqueFd() noexcept {
            reset();
        }

        CUniqueFd(const CUniqueFd&)            = delete;
        CUniqueFd& operator=(const CUniqueFd&) = delete;
        CUniqueFd(CUniqueFd&& other) noexcept : m_fd(other.release()) {}
        CUniqueFd& operator=(CUniqueFd&& other) noexcept {
            reset(other.release());
            return *this;
        }

        [[nodiscard]] int get() const noexcept {
            return m_fd;
        }
        [[nodiscard]] bool valid() const noexcept {
            return m_fd >= 0;
        }
        explicit operator bool() const noexcept {
            return valid();
        }

        // Gives up ownership without closing; the caller becomes responsible for the descriptor.
        [[nodiscard]] int release() noexcept {
            return std::exchange(m_fd, -1);
        }

        void reset(int fd = -1) noexcept {
            if (m_fd == fd) {
                return;
            }
            if (m_fd >= 0) {
                ::close(m_fd);
            }
            m_fd = fd;
        }

      private:
        int m_fd = -1;
    };
}
