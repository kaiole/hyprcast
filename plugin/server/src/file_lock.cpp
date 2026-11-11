#include "hyprcast/server/file_lock.hpp"

#include <fcntl.h>
#include <sys/file.h>

#include <cerrno>
#include <filesystem>
#include <system_error>

namespace hyprcast {

file_lock::file_lock(const std::filesystem::path& lock_path)
    : lock_fd_(::open(lock_path.c_str(), O_CREAT | O_CLOEXEC | O_RDWR, 0600)) {
    if (!lock_fd_) {
        throw std::system_error(errno,
                                std::generic_category(),
                                "Cannot open server lock '" + lock_path.string()
                                    + "'");
    }

    if (::flock(lock_fd_.get(), LOCK_EX | LOCK_NB) == -1) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "Cannot acquire server lock '" + lock_path.string()
                + "' (another hyprcast instance may be running)");
    }
}

} // namespace hyprcast
