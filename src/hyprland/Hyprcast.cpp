#include "Hyprcast.hpp"
#include "hyprcast/core/KeyEvent.hpp"
#include "hyprcast/core/KeyState.hpp"

#include <memory>
#include <print>

void CHyprcast::toggleCapture() {
    if (m_inputCapture) {
        m_inputCapture.reset();
        return;
    }

    m_inputCapture = std::make_unique<CInputCapture>(inputCaptureObserver);
}

void CHyprcast::inputCaptureObserver(const Hyprcast::SKeyEvent& keyEvent) {
    std::println(stderr, "[hyprcast] keycode: {} {} at {}", keyEvent.keycode, keyEvent.keyState == Hyprcast::eKeyState::PRESSED ? "pressed" : "released", keyEvent.timeMs);
}
