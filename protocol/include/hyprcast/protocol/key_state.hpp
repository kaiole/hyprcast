#pragma once

#include <cstdint>

namespace hyprcast {

// https://www.kernel.org/doc/html/latest/input/event-codes.html#ev-key
// https://wayland.app/protocols/wayland#wl_keyboard:enum:key_state
enum class key_state : std::uint8_t {
    released = 0,
    pressed = 1,
};

} // namespace hyprcast
