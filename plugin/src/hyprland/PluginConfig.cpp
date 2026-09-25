#include "PluginConfig.hpp"

#include "Plugin.hpp"

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/event/EventBus.hpp>

#include <stdexcept>
#include <utility>

namespace Hyprcast {
    namespace {
        CPluginConfig* instance = nullptr;
    }

    CPluginConfig::CPluginConfig(SCallbacks callbacks) : m_callbacks(std::move(callbacks)) {
        m_preReload = Event::bus()->m_events.config.preReload.listen([this] { m_config.beginReload(); });
        m_reloaded  = Event::bus()->m_events.config.reloaded.listen(
            [this] { runGuarded("Cannot apply keyboard subscriptions after config reload; shutting down plugin", [this] { handleConfigReload(); }); });

        if (!HyprlandAPI::addLuaFunction(PHANDLE, "hyprcast", "configure", &CPluginConfig::configure)) {
            throw std::runtime_error("Cannot register Lua function hyprcast.configure");
        }

        instance = this;
    }

    CPluginConfig::~CPluginConfig() {
        HyprlandAPI::removeLuaFunction(PHANDLE, "hyprcast", "configure");
        instance = nullptr;
    }

    int CPluginConfig::configure(lua_State* L) noexcept {
        return instance->m_config.configure(L);
    }

    void CPluginConfig::handleConfigReload() {
        m_config.finishReload(Config::mgr()->configVerifPassed() && Config::mgr()->getErrors().empty());
        m_callbacks.onConfigReload();
    }
}
