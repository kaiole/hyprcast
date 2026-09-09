#pragma once

#include "hyprcast/core/KeyboardId.hpp"

#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprutils/signal/Listener.hpp>
#include <string>

namespace Hyprcast {
    struct SKeyboardInfo {
        WP<IKeyboard>       keyboard;

        KeyboardId          id;
        std::string         name;
        bool                pendingRemoval = false;
        bool                subscribed     = false;

        CHyprSignalListener keyEventListener, modifiersListener, keymapListener, repeatInfoListener, destroyListener;
    };
}
