#include "Plugin.hpp"

#include "PluginConfig.hpp"
#include "Hyprcast.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
    std::unique_ptr<Hyprcast::CHyprcast>     g_hyprcast;
    std::unique_ptr<Hyprcast::CPluginConfig> g_config;
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    Hyprcast::PHANDLE = handle;

    const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
    const std::string CLIENT_HASH     = __hyprland_api_get_client_hash();

    if (COMPOSITOR_HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(Hyprcast::PHANDLE, "[Hyprcast] Mismatched headers! Can't proceed.", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[Hyprcast] Version mismatch");
    }

    auto config = std::make_unique<Hyprcast::CPluginConfig>();
    g_hyprcast  = std::make_unique<Hyprcast::CHyprcast>();
    g_config    = std::move(config);

    return {.name{Hyprcast::PLUGIN_NAME}, .description{Hyprcast::DESCRIPTION}, .author{Hyprcast::AUTHOR}, .version{Hyprcast::VERSION}};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_hyprcast.reset();
    g_config.reset();
}
