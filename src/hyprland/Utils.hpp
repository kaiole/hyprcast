#pragma once

#include "hyprcast/core/KeyEvent.hpp"
#include "hyprcast/core/KeymapEvent.hpp"
#include "hyprcast/core/ModifiersEvent.hpp"

#include <hyprland/src/devices/IKeyboard.hpp>

namespace Hyprcast::Utils {
    Hyprcast::SKeyEvent       toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeyEvent& event);
    Hyprcast::SModifiersEvent toHyprcastType(KeyboardId keyboardId, const IKeyboard::SModifiersEvent& event);
    Hyprcast::SKeymapEvent    toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeymapEvent& event);
}
