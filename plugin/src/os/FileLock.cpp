#include "FileLock.hpp"

#include <fcntl.h>
#include <sys/file.h>

#include <cerrno>
#include <filesystem>
#include <system_error>

namespace hyprcast {

CFileLock::CFileLock(const std::filesystem::path& lockPath)
    : m_lockFd(::open(lockPath.c_str(), O_CREAT | O_CLOEXEC | O_RDWR, 0600)) {
    if (!m_lockFd) {
        throw std::system_error(errno,
                                std::generic_category(),
                                "Cannot open server lock '" + lockPath.string()
                                    + "'");
    }

    if (::flock(m_lockFd.get(), LOCK_EX | LOCK_NB) == -1) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "Cannot acquire server lock '" + lockPath.string()
                + "' (another hyprcast instance may be running)");
    }
}

} // namespace hyprcast
