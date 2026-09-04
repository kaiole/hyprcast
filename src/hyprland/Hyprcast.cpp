#include "Hyprcast.hpp"
#include "hyprcast/core/KeyEvent.hpp"
#include "hyprcast/core/KeyState.hpp"

#include <memory>
#include <print>

void CHyprcast::startInputCapture() {
    if (m_inputCapture) {
        return;
    }

    m_inputCapture.reset();
    m_inputCapture = std::make_unique<CInputCapture>(inputCaptureObserver);
}

void CHyprcast::stopInputCapture() {
    m_inputCapture.reset();
}

void CHyprcast::inputCaptureObserver(const Hyprcast::SKeyEvent& keyEvent) {
    std::println(stderr, "[hyprcast] keycode: {} {}", keyEvent.keycode, keyEvent.keyState == Hyprcast::eKeyState::PRESSED ? "pressed" : "released");
}
