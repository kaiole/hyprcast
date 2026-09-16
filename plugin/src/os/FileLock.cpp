#include "FileLock.hpp"

#include <fcntl.h>
#include <filesystem>
#include <system_error>
#include <sys/file.h>

namespace Hyprcast {
    CFileLock::CFileLock(const std::filesystem::path& lockPath) : m_lockFd{} {
        int lockFd = ::open(lockPath.c_str(), O_CREAT | O_CLOEXEC | O_RDWR, 0600);
        if (lockFd == -1) {
            throw std::system_error(errno, std::generic_category(), "open");
        }

        m_lockFd.reset(lockFd);

        int flockStatus = ::flock(m_lockFd.getFd(), LOCK_EX | LOCK_NB);
        if (flockStatus == -1) {
            throw std::system_error(errno, std::generic_category(), "flock");
        }
    }
}
