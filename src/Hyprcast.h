#pragma once

#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprutils/signal/Listener.hpp>

class CHyprcast {
  public:
    void registerKeyEventListener();

  private:
    void                onKeyEvent(const IKeyboard::SKeyEvent& event);

    CHyprSignalListener m_keyEventListener;
};
