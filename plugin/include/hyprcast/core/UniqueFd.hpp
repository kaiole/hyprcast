#pragma once

namespace Hyprcast {
    class CUniqueFd {
      public:
        explicit CUniqueFd(int fd = -1) noexcept : m_fd(fd) {};
        ~CUniqueFd() noexcept;

        CUniqueFd(const CUniqueFd&)            = delete;
        CUniqueFd& operator=(const CUniqueFd&) = delete;
        CUniqueFd(CUniqueFd&&)                 = delete;
        CUniqueFd& operator=(CUniqueFd&&)      = delete;

        int        getFd() const noexcept {
            return m_fd;
        }

        void reset(int fd = -1);

      private:
        int m_fd;
    };
}
