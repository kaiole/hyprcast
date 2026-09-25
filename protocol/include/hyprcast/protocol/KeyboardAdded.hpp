#pragma once

#include "KeyboardId.hpp"

#include <string>

namespace Hyprcast {
    struct SKeyboardAdded {
        KeyboardId  id;
        std::string name;
    };
}
