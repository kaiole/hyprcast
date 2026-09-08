#pragma once

#include "hyprcast/config/Config.hpp"

#include <hyprland/src/helpers/signal/Signal.hpp>

namespace Hyprcast {
    class CPluginConfig {
      public:
        CPluginConfig();
        ~CPluginConfig();

        CPluginConfig(const CPluginConfig&)            = delete;
        CPluginConfig& operator=(const CPluginConfig&) = delete;
        CPluginConfig(CPluginConfig&&)                 = delete;
        CPluginConfig& operator=(CPluginConfig&&)      = delete;

        using ConfigReloadHandler = std::function<void()>;

        void           listen(ConfigReloadHandler configReloadHandler);

        const SConfig& getAcceptedConfig() const noexcept {
            return m_config.accepted();
        }

      private:
        static int          configure(lua_State* L) noexcept;

        CConfig             m_config;
        CHyprSignalListener m_preReload, m_reloaded;

        ConfigReloadHandler m_configReloadHandler;
    };
}
