#pragma once

#include "hyprcast/os/unique_fd.hpp"

#include <filesystem>

namespace hyprcast {

// Holds an exclusive flock() for as long as the object lives. All special
// members are implicit: unique_fd already makes this move-only (rule of zero).
class file_lock {
public:
    explicit file_lock(const std::filesystem::path& lock_path);

private:
    unique_fd lock_fd_;
};

} // namespace hyprcast
