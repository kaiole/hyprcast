#include "plugin_config.hpp"

#include "hyprcast/config/config.hpp"
#include "hyprcast/log/log_sink.hpp"
#include "plugin.hpp"

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <stdexcept>
#include <utility>

namespace hyprcast {
namespace {

// Lua calls configure() as a plain function, which cannot carry `this`. Null
// whenever no plugin_config exists to answer the call.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
config_state* live_config = nullptr;

int configure(lua_State* lua) noexcept {
    return call_configure(lua, live_config);
}

} // namespace

plugin_config::plugin_config(log_sink& log, callbacks cbs)
    : log_(&log),
      callbacks_(std::move(cbs)) {
    pre_reload_ = Event::bus()->m_events.config.preReload.listen(
        [this] { config_.begin_reload(); });
    reloaded_ = Event::bus()->m_events.config.reloaded.listen([this] {
        run_guarded("Cannot apply keyboard subscriptions after config reload; "
                    "shutting down plugin",
                    [this] { handle_config_reload(); });
    });

    if (!HyprlandAPI::addLuaFunction(plugin_handle,
                                     "hyprcast",
                                     "configure",
                                     &configure)) {
        throw std::runtime_error(
            "Cannot register Lua function hyprcast.configure");
    }

    live_config = &config_;
}

plugin_config::~plugin_config() {
    live_config = nullptr;
    HyprlandAPI::removeLuaFunction(plugin_handle, "hyprcast", "configure");
}

void plugin_config::handle_config_reload() {
    config_.finish_reload(Config::mgr()->configVerifPassed()
                          && Config::mgr()->getErrors().empty());
    callbacks_.on_config_reload();
}

} // namespace hyprcast
