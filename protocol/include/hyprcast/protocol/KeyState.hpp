#pragma once

#include <cstdint>

namespace Hyprcast {
    // https://www.kernel.org/doc/html/latest/input/event-codes.html#ev-key
    // https://wayland.app/protocols/wayland#wl_keyboard:enum:key_state
    enum class eKeyState : std::uint8_t {
        RELEASED = 0,
        PRESSED  = 1
    };
}
