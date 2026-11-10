#pragma once

#include "key_state.hpp"

#include <cstdint>

namespace hyprcast {

struct key_event {
    std::uint32_t time_ms = 0;
    std::uint32_t keycode = 0;
    key_state state = key_state::released;
};

} // namespace hyprcast
