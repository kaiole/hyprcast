#pragma once

#include "KeyboardInfo.hpp"
#include "hyprcast/config/Config.hpp"
#include "hyprcast/core/KeyEvent.hpp"
#include "hyprcast/core/KeyboardId.hpp"
#include "hyprcast/core/KeymapEvent.hpp"
#include "hyprcast/core/ModifiersEvent.hpp"

#include <hyprland/src/helpers/memory/Memory.hpp>

#include <memory>
#include <vector>

struct wl_event_source;

namespace Hyprcast {
    class CKeyboardRegistry {
      public:
        CKeyboardRegistry();
        ~CKeyboardRegistry();

        CKeyboardRegistry(const CKeyboardRegistry&)            = delete;
        CKeyboardRegistry& operator=(const CKeyboardRegistry&) = delete;
        CKeyboardRegistry(CKeyboardRegistry&&)                 = delete;
        CKeyboardRegistry& operator=(CKeyboardRegistry&&)      = delete;

        void               addKeyboard(SP<IKeyboard> keyboard);
        void               subscribeEventListeners(const SConfig& acceptedConfig);

      private:
        // TODO: might not need in class
        SKeyEvent                                   toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeyEvent& event) noexcept;
        SModifiersEvent                             toHyprcastType(KeyboardId keyboardId, const IKeyboard::SModifiersEvent& event) noexcept;
        SKeymapEvent                                toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeymapEvent& event);

        void                                        unsubscribeListeners(KeyboardId id);

        void                                        scheduleRemoval(SKeyboardInfo& keyboardInfo);

        KeyboardId                                  m_nextId = 1;
        std::vector<std::unique_ptr<SKeyboardInfo>> m_keyboardRegistry;

        wl_event_source*                            m_removalSource = nullptr;
    };
}
