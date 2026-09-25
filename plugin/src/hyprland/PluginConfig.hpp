#pragma once

#include "hyprcast/config/Config.hpp"
#include "CallbackBoundary.hpp"

#include <hyprland/src/helpers/signal/Signal.hpp>

#include <functional>
#include <string_view>
#include <utility>

namespace Hyprcast {
    class CPluginConfig {
      public:
        using ConfigReloadCB = std::function<void()>;
        using ExceptionCB    = std::move_only_function<void() noexcept>;

        struct SCallbacks {
            ConfigReloadCB onConfigReload;
            ExceptionCB    onException;
        };

        explicit CPluginConfig(SCallbacks callbacks);
        ~CPluginConfig();

        CPluginConfig(const CPluginConfig&)            = delete;
        CPluginConfig& operator=(const CPluginConfig&) = delete;
        CPluginConfig(CPluginConfig&&)                 = delete;
        CPluginConfig& operator=(CPluginConfig&&)      = delete;

        const SConfig& getAcceptedConfig() const noexcept {
            return m_config.accepted();
        }

      private:
        static int configure(lua_State* L) noexcept;

        void       handleConfigReload();

        template <typename Function>
        void runGuarded(std::string_view context, Function&& function) {
            Hyprcast::runGuarded(context, std::forward<Function>(function), m_callbacks.onException);
        }

        CConfig             m_config;
        CHyprSignalListener m_preReload, m_reloaded;

        SCallbacks          m_callbacks;
    };
}
