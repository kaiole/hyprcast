#pragma once

#include "hyprcast/core/KeyEvent.hpp"

#include <hyprland/src/event/EventBus.hpp>

#include <functional>

class CInputCapture {
  public:
    using Observer = std::function<void(const Hyprcast::SKeyEvent&)>;

    CInputCapture(Observer observer);

    CInputCapture(const CInputCapture&)       = delete;
    CInputCapture& operator=(CInputCapture&)  = delete;
    CInputCapture(CInputCapture&&)            = delete;
    CInputCapture& operator=(CInputCapture&&) = delete;

  private:
    Observer            m_observer;
    CHyprSignalListener m_keyEventListener;
};
