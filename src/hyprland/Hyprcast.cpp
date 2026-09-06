#include "Hyprcast.hpp"

#include "KeyboardInfo.hpp"
#include "hyprcast/core/KeyState.hpp"

#include <hyprland/src/managers/input/InputManager.hpp>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdlib>
#include <memory>
#include <print>
#include <utility>

namespace Hyprcast {
    CHyprcast::CHyprcast() {
        for (auto& keyboard : g_pInputManager->m_keyboards) {
            registerKeyboard(keyboard);
        }
    }

    void CHyprcast::registerKeyboard(SP<IKeyboard> keyboard) {
        auto keyboardInfo = std::make_unique<SKeyboardInfo>();

        keyboardInfo->keyboardId = nextId++;
        keyboardInfo->keyboard   = keyboard;

        keyboardInfo->keyEvent = keyboard->m_keyboardEvents.key.listen([this, keyboardId = keyboardInfo->keyboardId](const IKeyboard::SKeyEvent& event) {
            auto keyEvent = toHyprcastType(keyboardId, event);
            std::println(stderr, "[hyprcast] keycode: {} {} at {}", keyEvent.keycode, keyEvent.state == eKeyState::PRESSED ? "pressed" : "released", keyEvent.timeMs);

            // TODO: queue IPC message
        });

        keyboardInfo->modifiersEvent = keyboard->m_keyboardEvents.modifiers.listen([this, keyboardId = keyboardInfo->keyboardId](const IKeyboard::SModifiersEvent& event) {
            auto modifiersEvent = toHyprcastType(keyboardId, event);

            // TODO: queue IPC message
        });

        keyboardInfo->keymapEvent = keyboard->m_keyboardEvents.keymap.listen([this, keyboardId = keyboardInfo->keyboardId](const IKeyboard::SKeymapEvent& event) {
            auto keymapEvent = toHyprcastType(keyboardId, event);

            // TODO: queue IPC message
        });

        // TODO: repeatInfo and destroy

        keyboards.push_back(std::move(keyboardInfo));
    }

    SKeyEvent CHyprcast::toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeyEvent& event) noexcept {
        auto state = event.state == WL_KEYBOARD_KEY_STATE_PRESSED ? eKeyState::PRESSED : eKeyState::RELEASED;

        return {.keyboardId = keyboardId, .timeMs = event.timeMs, .keycode = event.keycode, .state = state};
    }

    SModifiersEvent CHyprcast::toHyprcastType(KeyboardId keyboardId, const IKeyboard::SModifiersEvent& event) noexcept {
        return {.keyboardId = keyboardId, .depressed = event.depressed, .latched = event.latched, .locked = event.locked, .group = event.group};
    }

    SKeymapEvent CHyprcast::toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeymapEvent& event) {
        std::unique_ptr<char, decltype(&std::free)> keymapString{xkb_keymap_get_as_string(event.keymap, XKB_KEYMAP_FORMAT_TEXT_V1), &std::free};

        return {.keyboardId = keyboardId, .keymap = keymapString.get()};
    }
}
