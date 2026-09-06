#pragma once

#include "KeyboardId.hpp"

#include <string>

namespace Hyprcast {
    struct SKeymapEvent {
        KeyboardId  keyboardId;
        std::string keymap;
    };
}
