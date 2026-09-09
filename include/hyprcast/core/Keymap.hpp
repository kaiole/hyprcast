#pragma once

#include "KeyboardId.hpp"

#include <string>

namespace Hyprcast {
    struct SKeymap {
        KeyboardId  keyboardId;
        std::string keymap;
    };
}
