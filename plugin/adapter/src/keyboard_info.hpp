#pragma once

#include "hyprcast/protocol/keyboard_id.hpp"

#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprutils/signal/Listener.hpp>

#include <string>

namespace hyprcast {

struct keyboard_info {
    WP<IKeyboard> keyboard;

    keyboard_id id = 0;
    std::string name;

    // Hyprland's layout notification does not say whether the keymap changed.
    // Track what was sent so that only changes are reported.
    std::string last_sent_keymap;

    bool pending_removal = false;
    bool subscribed = false;

    CHyprSignalListener key_event_listener, modifiers_listener,
        repeat_info_listener, destroy_listener;
};

} // namespace hyprcast
