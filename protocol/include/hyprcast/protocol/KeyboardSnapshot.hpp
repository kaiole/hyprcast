#pragma once

#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/Keymap.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"

#include <string>

namespace hyprcast {

struct keyboard_snapshot {
    keyboard_id id = 0;
    std::string name;
    modifier_state modifiers;
    keymap_text keymap;
    hyprcast::repeat_info repeat_info;
};

} // namespace hyprcast
