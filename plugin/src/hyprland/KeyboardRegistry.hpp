#pragma once

#include "KeyboardInfo.hpp"
#include "hyprcast/config/Config.hpp"
#include "hyprcast/protocol/KeyEvent.hpp"
#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/KeyboardSnapshot.hpp"
#include "hyprcast/protocol/Keymap.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RegistrySnapshot.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"
#include "wayland/WlEventSource.hpp"

#include <functional>
#include <hyprland/src/helpers/memory/Memory.hpp>

#include <memory>
#include <string>
#include <vector>

struct wl_event_source;

namespace Hyprcast {
    class CKeyboardRegistry {
      public:
        using SubscriptionCB    = std::function<void(SKeyboardSnapshot)>;
        using UnsubscriptionCB  = std::function<void(KeyboardId)>;
        using KeyEventCB        = std::function<void(KeyboardId, SKeyEvent)>;
        using ModifiersEventCB  = std::function<void(KeyboardId, SModifiers)>;
        using KeymapEventCB     = std::function<void(KeyboardId, Keymap)>;
        using RepeatInfoEventCB = std::function<void(KeyboardId, SRepeatInfo)>;

        struct SCallbacks {
            SubscriptionCB    handleSubscription;
            UnsubscriptionCB  handleUnsubscription;
            KeyEventCB        handleKeyEvent;
            ModifiersEventCB  handleModifiers;
            KeymapEventCB     handleKeymap;
            RepeatInfoEventCB handleRepeatInfo;
        };

        explicit CKeyboardRegistry(SCallbacks callbacks);
        ~CKeyboardRegistry() = default;

        CKeyboardRegistry(const CKeyboardRegistry&)                   = delete;
        CKeyboardRegistry& operator=(const CKeyboardRegistry&)        = delete;
        CKeyboardRegistry(CKeyboardRegistry&&)                        = delete;
        CKeyboardRegistry&             operator=(CKeyboardRegistry&&) = delete;

        void                           addKeyboard(SP<IKeyboard> keyboard);
        void                           updateSubscriptions(const SConfig& acceptedConfig);

        [[nodiscard]] RegistrySnapshot getRegistrySnapshot();

      private:
        void                                        scheduleRemoval(SKeyboardInfo& keyboardInfo) noexcept;
        void                                        unsubscribeListeners(SKeyboardInfo& keyboardInfo);

        [[nodiscard]] SKeyboardSnapshot             getKeyboardSnapshot(const IKeyboard& keyboard, KeyboardId id, const std::string& name);

        SCallbacks                                  m_callbacks;
        KeyboardId                                  m_nextId = 1;
        std::vector<std::unique_ptr<SKeyboardInfo>> m_keyboardRegistry;

        CWlEventSource                              m_wlIdleKeyboardRemoval;
    };
}
