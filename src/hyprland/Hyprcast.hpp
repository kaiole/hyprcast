#pragma once

#include "InputCapture.hpp"
#include "hyprcast/core/KeyEvent.hpp"

#include <memory>

class CHyprcast {
  public:
    CHyprcast() = default;

    CHyprcast(const CHyprcast&)             = delete;
    CHyprcast& operator=(const CHyprcast&)  = delete;
    CHyprcast(const CHyprcast&&)            = delete;
    CHyprcast& operator=(const CHyprcast&&) = delete;

    void       startInputCapture();
    void       stopInputCapture();

  private:
    static void                    inputCaptureObserver(const Hyprcast::SKeyEvent& keyEvent);

    std::unique_ptr<CInputCapture> m_inputCapture;
};
