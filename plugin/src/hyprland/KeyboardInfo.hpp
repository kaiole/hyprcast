#pragma once

#include "hyprcast/protocol/KeyboardId.hpp"

#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprutils/signal/Listener.hpp>
#include <string>

namespace Hyprcast {
    struct SKeyboardInfo {
        WP<IKeyboard> keyboard;

        KeyboardId  id;
        std::string name;

        // Hyprland can replace a keymap during config reload without notifying
        // keymapListener. Track what we sent so layout updates can detect changes.
        std::string lastSentKeymap;

        bool pendingRemoval = false;
        bool subscribed     = false;

        CHyprSignalListener keyEventListener, modifiersListener, keymapListener, repeatInfoListener, destroyListener;
    };
}
