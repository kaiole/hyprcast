#pragma once

#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/keymap_text.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/repeat_info.hpp"

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
