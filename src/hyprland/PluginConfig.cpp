#include "PluginConfig.hpp"

#include "Plugin.hpp"

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/event/EventBus.hpp>

#include <stdexcept>

namespace Hyprcast {
    namespace {
        CPluginConfig* instance = nullptr;
    }

    CPluginConfig::CPluginConfig() {
        m_preReload = Event::bus()->m_events.config.preReload.listen([this] { m_config.beginReload(); });
        m_reloaded  = Event::bus()->m_events.config.reloaded.listen([this] {
            m_config.finishReload(Config::mgr()->configVerifPassed() && Config::mgr()->getErrors().empty());
            m_configReloadHandler();
        });

        if (!HyprlandAPI::addLuaFunction(PHANDLE, "hyprcast", "configure", &CPluginConfig::configure)) {
            throw std::runtime_error("[Hyprcast] Could not register Lua configuration function");
        }

        instance = this;
    }

    CPluginConfig::~CPluginConfig() {
        HyprlandAPI::removeLuaFunction(PHANDLE, "hyprcast", "configure");
        instance = nullptr;
    }

    void CPluginConfig::listen(ConfigReloadHandler configReloadHandler) {
        m_configReloadHandler = std::move(configReloadHandler);
    }

    int CPluginConfig::configure(lua_State* L) noexcept {
        return instance->m_config.configure(L);
    }
}
