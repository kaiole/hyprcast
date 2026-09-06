#pragma once

#include "hyprcast/config/Config.hpp"

#include <hyprland/src/helpers/signal/Signal.hpp>

namespace Hyprcast {
    class CPluginConfig {
      public:
        CPluginConfig();

        CPluginConfig(const CPluginConfig&)            = delete;
        CPluginConfig& operator=(const CPluginConfig&) = delete;

        ~CPluginConfig();

        const SConfig& accepted() const noexcept {
            return m_config.accepted();
        }

      private:
        CConfig             m_config;
        CHyprSignalListener m_preReload, m_reloaded;
        static int          configure(lua_State* L) noexcept;
    };

    // Valid between successful plugin initialization and plugin exit. No capture changes yet.
    const SConfig& getCurrentConfig() noexcept;
}
