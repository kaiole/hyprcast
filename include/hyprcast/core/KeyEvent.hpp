#pragma once

#include "KeyState.hpp"

namespace Hyprcast {
    struct SKeyEvent {
        uint32_t  keycode;
        eKeyState keyState;
    };
}
