#pragma once

#include "KeyState.hpp"

#include <cstdint>

namespace Hyprcast {
    struct SKeyEvent {
        std::uint32_t timeMs;
        std::uint32_t keycode;
        eKeyState     state;
    };
}
