#pragma once

#include "hyprland/KeyboardRegistry.hpp"
#include "hyprland/PluginConfig.hpp"
#include "ipc/EventServer.hpp"

#include <helpers/memory/Memory.hpp>

namespace Hyprcast {
    class CHyprcast {
      public:
        CHyprcast();
        ~CHyprcast() = default;

        CHyprcast(const CHyprcast&)                = delete;
        CHyprcast& operator=(const CHyprcast&)     = delete;
        CHyprcast(CHyprcast&&) noexcept            = delete;
        CHyprcast& operator=(CHyprcast&&) noexcept = delete;

        void       addKeyboard(SP<IKeyboard> keyboard);

      private:
        void                          requestRegistrySnapshot();
        CKeyboardRegistry::SCallbacks makeRegistryCallbacks();
        CPluginConfig::SCallbacks     makePluginConfigCallbacks();

        void                          shutdown() noexcept;

        Hyprcast::CEventServer        m_socket;
        Hyprcast::CKeyboardRegistry   m_keyboardRegistry;
        Hyprcast::CPluginConfig       m_config;

        bool                          m_shutdown = false;
    };
}
