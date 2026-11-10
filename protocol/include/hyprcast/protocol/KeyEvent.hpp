#pragma once

#include "KeyState.hpp"

#include <cstdint>

namespace hyprcast {

struct SKeyEvent {
    std::uint32_t timeMs = 0;
    std::uint32_t keycode = 0;
    eKeyState state = eKeyState::RELEASED;
};

} // namespace hyprcast
