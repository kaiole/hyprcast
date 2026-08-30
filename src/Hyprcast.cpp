#include "Hyprcast.h"

#include <print>

void CHyprcast::registerKeyEventListener() {
    m_keyEventListener = Event::bus()->m_events.input.keyboard.key.listen([this](const IKeyboard::SKeyEvent& event, Event::SCallbackInfo) { onKeyEvent(event); });
}

void CHyprcast::onKeyEvent(const IKeyboard::SKeyEvent& event) {
    std::println(stderr, "[hyprcast]: keycode {}, state {}", event.keycode, event.state ? "pressed" : "released");
}
