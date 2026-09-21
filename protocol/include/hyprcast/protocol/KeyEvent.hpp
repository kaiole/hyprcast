#pragma once

#include "KeyState.hpp"

namespace Hyprcast {
    struct SKeyEvent {
        uint32_t  timeMs;
        uint32_t  keycode;
        eKeyState state;
    };
}
