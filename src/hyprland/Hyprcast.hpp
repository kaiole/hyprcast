#pragma once

#include "KeyboardInfo.hpp"
#include "hyprcast/core/KeyEvent.hpp"
#include "hyprcast/core/KeymapEvent.hpp"
#include "hyprcast/core/ModifiersEvent.hpp"

#include <hyprland/src/helpers/memory/Memory.hpp>

#include <memory>
#include <vector>

class CHyprcast {
  public:
    CHyprcast();

    CHyprcast(const CHyprcast&)            = delete;
    CHyprcast& operator=(const CHyprcast&) = delete;
    CHyprcast(CHyprcast&&)                 = delete;
    CHyprcast& operator=(CHyprcast&&)      = delete;

    void       registerKeyboard(SP<IKeyboard> keyboard);

  private:
    Hyprcast::SKeyEvent       toHyprcastType(Hyprcast::KeyboardId keyboardId, const IKeyboard::SKeyEvent& event);
    Hyprcast::SModifiersEvent toHyprcastType(Hyprcast::KeyboardId keyboardId, const IKeyboard::SModifiersEvent& event);
    Hyprcast::SKeymapEvent    toHyprcastType(Hyprcast::KeyboardId keyboardId, const IKeyboard::SKeymapEvent& event);

    // Reserve 0 as fail state
    Hyprcast::KeyboardId                                  nextId = 1;
    std::vector<std::unique_ptr<Hyprcast::SKeyboardInfo>> keyboards;
};
