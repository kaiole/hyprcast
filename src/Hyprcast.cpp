#include "Hyprcast.h"

#include <memory>
#include <print>

void CHyprcast::startInputCapture() {
    if (m_inputCapture) {
        return;
    }

    m_inputCapture.reset();
    m_inputCapture = std::make_unique<CInputCapture>(observer);
}

void CHyprcast::observer(const IKeyboard::SKeyEvent& keyEvent, const Event::SCallbackInfo&) {
    std::println(stderr, "[hyprcast] keycode: {} {}", keyEvent.keycode, keyEvent.state ? "pressed" : "released");
}
