#pragma once

#include <cstdint>

namespace hyprcast {

struct repeat_info {
    std::uint32_t rate = 0;  // Repeats per second; 0 disables repeat.
    std::uint32_t delay = 0; // Milliseconds before the first repeat.
};

} // namespace hyprcast
