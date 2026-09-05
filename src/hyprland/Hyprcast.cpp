#include "Hyprcast.hpp"

#include "KeyboardInfo.hpp"
#include "hyprcast/core/KeyState.hpp"

#include <hyprland/src/managers/input/InputManager.hpp>

#include <memory>
#include <wayland-server-protocol.h>

CHyprcast::CHyprcast() {
    for (auto& keyboard : g_pInputManager->m_keyboards) {
        registerKeyboard(keyboard);
    }
};

void CHyprcast::registerKeyboard(SP<IKeyboard> keyboard) {
    auto keyboardInfo = std::make_unique<Hyprcast::SKeyboardInfo>();

    keyboardInfo->keyboardId = nextId++;
    keyboardInfo->keyboard   = keyboard;

    keyboardInfo->keyEventListener = keyboard->m_keyboardEvents.key.listen([this, keyboardId = keyboardInfo->keyboardId](const IKeyboard::SKeyEvent& event) {
        auto keyEvent = toHyprcastType(keyboardId, event);
        std::println(stderr, "[hyprcast] keycode: {} {} at {}", keyEvent.keycode, keyEvent.state == Hyprcast::eKeyState::PRESSED ? "pressed" : "released", keyEvent.timeMs);

        // TODO: queue IPC message
    });

    keyboardInfo->modifiersEventListener = keyboard->m_keyboardEvents.modifiers.listen([this, keyboardId = keyboardInfo->keyboardId](const IKeyboard::SModifiersEvent& event) {
        auto modifiersEvent = toHyprcastType(keyboardId, event);

        // TODO: queue IPC message
    });

    keyboardInfo->keymapEventListener = keyboard->m_keyboardEvents.keymap.listen([this, keyboardId = keyboardInfo->keyboardId](const IKeyboard::SKeymapEvent& event) {
        auto keymapEvent = toHyprcastType(keyboardId, event);

        // TODO: queue IPC message
    });

    // TODO: repeatInfo and destroy

    keyboards.push_back(std::move(keyboardInfo));
}

Hyprcast::SKeyEvent CHyprcast::toHyprcastType(Hyprcast::KeyboardId keyboardId, const IKeyboard::SKeyEvent& event) {
    auto state = event.state == WL_KEYBOARD_KEY_STATE_PRESSED ? Hyprcast::eKeyState::PRESSED : Hyprcast::eKeyState::RELEASED;

    return {.keyboardId = keyboardId, .timeMs = event.timeMs, .keycode = event.keycode, .state = state};
}
Hyprcast::SModifiersEvent CHyprcast::toHyprcastType(Hyprcast::KeyboardId keyboardId, const IKeyboard::SModifiersEvent& event) {
    return {.keyboardId = keyboardId, .depressed = event.depressed, .latched = event.latched, .locked = event.locked, .group = event.group};
}
Hyprcast::SKeymapEvent CHyprcast::toHyprcastType(Hyprcast::KeyboardId keyboardId, const IKeyboard::SKeymapEvent& event) {
    std::unique_ptr<char, decltype(&std::free)> keymapString{xkb_keymap_get_as_string(event.keymap, XKB_KEYMAP_FORMAT_TEXT_V1), &std::free};

    return {.keyboardId = keyboardId, .keymap = keymapString.get()};
}
