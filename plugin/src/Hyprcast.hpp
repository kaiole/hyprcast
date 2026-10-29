#pragma once

#include "hyprland/KeyboardRegistry.hpp"
#include "hyprland/PluginConfig.hpp"
#include "ipc/EventServer.hpp"

#include <helpers/memory/Memory.hpp>

namespace Hyprcast {
    class CHyprcast {
      public:
        CHyprcast();

        // Members hold callbacks that capture `this`, so the object must never move.
        CHyprcast(const CHyprcast&)            = delete;
        CHyprcast& operator=(const CHyprcast&) = delete;
        CHyprcast(CHyprcast&&)                 = delete;
        CHyprcast& operator=(CHyprcast&&)      = delete;

        void       addKeyboard(const SP<IKeyboard>& keyboard);

        void       setPause(bool paused);
        bool       isPaused() const noexcept;

      private:
        void                          requestRegistrySnapshot();
        CKeyboardRegistry::SCallbacks makeRegistryCallbacks();
        CPluginConfig::SCallbacks     makePluginConfigCallbacks();

        void                          shutdown() noexcept;

        CEventServer                  m_server;
        CKeyboardRegistry             m_keyboardRegistry;
        CPluginConfig                 m_config;

        bool                          m_shutdown = false;
        bool                          m_paused   = true;
    };
}
