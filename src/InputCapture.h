#pragma once

#include <hyprland/src/event/EventBus.hpp>

#include <functional>

class CInputCapture {
  public:
    using Observer = std::function<void(const IKeyboard::SKeyEvent&, const Event::SCallbackInfo&)>;

    CInputCapture(Observer observer);

    CInputCapture(const CInputCapture&)            = delete;
    CInputCapture& operator=(const CInputCapture&) = delete;

  private:
    CHyprSignalListener m_keyEventListener;
};
