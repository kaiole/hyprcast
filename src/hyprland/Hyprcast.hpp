#pragma once

#include "KeyboardInfo.hpp"
#include "hyprcast/core/KeyEvent.hpp"
#include "hyprcast/core/KeymapEvent.hpp"
#include "hyprcast/core/ModifiersEvent.hpp"

#include <hyprland/src/helpers/memory/Memory.hpp>

#include <memory>
#include <vector>

namespace Hyprcast {
    class CHyprcast {
      public:
        CHyprcast();

        CHyprcast(const CHyprcast&)            = delete;
        CHyprcast& operator=(const CHyprcast&) = delete;
        CHyprcast(CHyprcast&&)                 = delete;
        CHyprcast& operator=(CHyprcast&&)      = delete;

        void       registerKeyboard(SP<IKeyboard> keyboard);

      private:
        SKeyEvent       toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeyEvent& event) noexcept;
        SModifiersEvent toHyprcastType(KeyboardId keyboardId, const IKeyboard::SModifiersEvent& event) noexcept;
        SKeymapEvent    toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeymapEvent& event);

        // Reserve 0 as fail state
        KeyboardId                                  nextId = 1;
        std::vector<std::unique_ptr<SKeyboardInfo>> keyboards;
    };
}
