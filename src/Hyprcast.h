#pragma once

#include "InputCapture.h"

#include <memory>

class CHyprcast {
  public:
    CHyprcast() = default;

    CHyprcast(const CHyprcast&)            = delete;
    CHyprcast& operator=(const CHyprcast&) = delete;

    void       startInputCapture();

  private:
    static void                    observer(const IKeyboard::SKeyEvent& keyEvent, const Event::SCallbackInfo&);

    std::unique_ptr<CInputCapture> m_inputCapture;
};
