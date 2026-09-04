#pragma once

#include "InputCapture.hpp"
#include "hyprcast/core/KeyEvent.hpp"

#include <memory>

class CHyprcast {
  public:
    CHyprcast() = default;

    CHyprcast(const CHyprcast&)       = delete;
    CHyprcast& operator=(CHyprcast&)  = delete;
    CHyprcast(const CHyprcast&&)      = delete;
    CHyprcast& operator=(CHyprcast&&) = delete;

    void       toggleCapture();

  private:
    static void                    inputCaptureObserver(const Hyprcast::SKeyEvent& keyEvent);

    std::unique_ptr<CInputCapture> m_inputCapture;
};
