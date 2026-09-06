#pragma once

#include "hyprcast/core/KeyboardId.hpp"

#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprutils/signal/Listener.hpp>

namespace Hyprcast {
    struct SKeyboardInfo {
        KeyboardId          keyboardId;
        WP<IKeyboard>       keyboard;

        CHyprSignalListener keyEvent, modifiersEvent, keymapEvent, repeatInfoEvent, destroyEvent;
    };
}
