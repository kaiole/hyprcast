#pragma once

#include <cstdint>

namespace Hyprcast {
    // https://www.kernel.org/doc/html/latest/input/event-codes.html#ev-key
    // https://wayland.app/protocols/wayland#wl_keyboard:enum:key_state
    enum class eKeyState : uint8_t {
        PRESSED  = 0,
        RELEASED = 1
    };
}
