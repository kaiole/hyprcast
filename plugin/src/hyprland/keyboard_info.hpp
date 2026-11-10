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

    // Hyprland can replace a keymap during config reload without notifying
    // keymap_listener. Track what we sent so layout updates can detect changes.
    std::string last_sent_keymap;

    bool pending_removal = false;
    bool subscribed = false;

    CHyprSignalListener key_event_listener, modifiers_listener, keymap_listener,
        repeat_info_listener, destroy_listener;
};

} // namespace hyprcast
