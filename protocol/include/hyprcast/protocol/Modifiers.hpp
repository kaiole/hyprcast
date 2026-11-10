#pragma once

#include <cstdint>

namespace hyprcast {

struct SModifiers {
    std::uint32_t depressed = 0;
    std::uint32_t latched = 0;
    std::uint32_t locked = 0;
    std::uint32_t group = 0;
};

} // namespace hyprcast
