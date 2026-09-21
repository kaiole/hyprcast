#pragma once

#include "KeyboardInfo.hpp"
#include "hyprcast/config/Config.hpp"
#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/Keymap.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"
#include "wayland/WlEventSource.hpp"

#include <functional>
#include <hyprland/src/helpers/memory/Memory.hpp>

#include <memory>
#include <vector>

struct wl_event_source;

namespace Hyprcast {
    class CKeyboardRegistry {
      public:
        using ModifiersEventCB  = std::function<void(KeyboardId, const SModifiers&)>;
        using KeymapEventCB     = std::function<void(KeyboardId, const Keymap&)>;
        using RepeatInfoEventCB = std::function<void(KeyboardId, const SRepeatInfo&)>;

        struct SCallbacks {
            ModifiersEventCB  handleModifiers;
            KeymapEventCB     handleKeymap;
            RepeatInfoEventCB handleRepeatInfo;
        };

        explicit CKeyboardRegistry(SCallbacks callbacks);
        ~CKeyboardRegistry() = default;

        CKeyboardRegistry(const CKeyboardRegistry&)            = delete;
        CKeyboardRegistry& operator=(const CKeyboardRegistry&) = delete;
        CKeyboardRegistry(CKeyboardRegistry&&)                 = delete;
        CKeyboardRegistry& operator=(CKeyboardRegistry&&)      = delete;

        void               addKeyboard(SP<IKeyboard> keyboard);
        void               updateSubscriptions(const SConfig& acceptedConfig);

      private:
        void                                        unsubscribeListeners(SKeyboardInfo& keyboardInfo) noexcept;
        void                                        scheduleRemoval(SKeyboardInfo& keyboardInfo) noexcept;

        SCallbacks                                  m_callbacks;
        KeyboardId                                  m_nextId = 1;
        std::vector<std::unique_ptr<SKeyboardInfo>> m_keyboardRegistry;

        CWlEventSource                              m_wlIdleKeyboardRemoval;
    };
}
