#include "Utils.hpp"

#include "hyprcast/core/KeyEvent.hpp"
#include "hyprcast/core/KeyState.hpp"
#include "hyprcast/core/KeymapEvent.hpp"

#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <memory>

namespace Hyprcast::Utils {
    Hyprcast::SKeyEvent toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeyEvent& event) {
        auto state = event.state == WL_KEYBOARD_KEY_STATE_PRESSED ? Hyprcast::eKeyState::PRESSED : Hyprcast::eKeyState::RELEASED;

        return {.keyboardId = keyboardId, .timeMs = event.timeMs, .keycode = event.keycode, .state = state};
    }

    Hyprcast::SModifiersEvent toHyprcastType(KeyboardId keyboardId, const IKeyboard::SModifiersEvent& event) {
        return {.keyboardId = keyboardId, .depressed = event.depressed, .latched = event.latched, .locked = event.locked, .group = event.group};
    }

    Hyprcast::SKeymapEvent toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeymapEvent& event) {
        std::unique_ptr<char, decltype(&std::free)> keymapString{xkb_keymap_get_as_string(event.keymap, XKB_KEYMAP_FORMAT_TEXT_V1), &std::free};

        return {.keyboardId = keyboardId, .keymap = keymapString.get()};
    }
}
