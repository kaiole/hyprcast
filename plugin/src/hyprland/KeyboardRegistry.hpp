#pragma once

#include "KeyboardInfo.hpp"
#include "hyprcast/config/Config.hpp"
#include "hyprcast/core/KeyboardId.hpp"
#include "hyprcast/core/WlEventSource.hpp"

#include <hyprland/src/helpers/memory/Memory.hpp>

#include <memory>
#include <vector>

struct wl_event_source;

namespace Hyprcast {
    class CKeyboardRegistry {
      public:
        CKeyboardRegistry();
        ~CKeyboardRegistry() = default;

        CKeyboardRegistry(const CKeyboardRegistry&)            = delete;
        CKeyboardRegistry& operator=(const CKeyboardRegistry&) = delete;
        CKeyboardRegistry(CKeyboardRegistry&&)                 = delete;
        CKeyboardRegistry& operator=(CKeyboardRegistry&&)      = delete;

        void               addKeyboard(SP<IKeyboard> keyboard);
        void               subscribeEventListeners(const SConfig& acceptedConfig);

      private:
        void unsubscribeListeners(KeyboardId id);
        void scheduleRemoval(SKeyboardInfo& keyboardInfo);

        struct SEventSourceRemover {
            void operator()(wl_event_source* eventSource) const;
        };

        KeyboardId                                  m_nextId = 1;
        std::vector<std::unique_ptr<SKeyboardInfo>> m_keyboardRegistry;

        CWlEventSource                              m_eventSource;
    };
}
