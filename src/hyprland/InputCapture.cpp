#include "InputCapture.hpp"

#include "hyprcast/core/KeyEvent.hpp"
#include "hyprcast/core/KeyState.hpp"

CInputCapture::CInputCapture(Observer observer) : m_observer(std::move(observer)) {
    m_keyEventListener = Event::bus()->m_events.input.keyboard.key.listen([this](const IKeyboard::SKeyEvent& event, const Event::SCallbackInfo&) {
        const auto                keyState = event.state == WL_KEYBOARD_KEY_STATE_PRESSED ? Hyprcast::eKeyState::PRESSED : Hyprcast::eKeyState::RELEASED;
        const Hyprcast::SKeyEvent keyEvent{.keycode = event.keycode, .keyState = keyState};

        m_observer(keyEvent);
    });
}
