#pragma once

#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/Keymap.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"

#include <string>

namespace Hyprcast {
    struct SKeyboardSnapshot {
        KeyboardId  id;
        std::string name;
        SModifiers  modifiers;
        Keymap      keymap;
        SRepeatInfo repeatInfo;
    };
}
