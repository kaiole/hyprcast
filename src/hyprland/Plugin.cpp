#include "Plugin.hpp"
#include "Hyprcast.hpp"

#include <memory>

static std::unique_ptr<CHyprcast> g_hyprcast;

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
    const std::string CLIENT_HASH     = __hyprland_api_get_client_hash();

    if (COMPOSITOR_HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[Hyprcast] Mismatched headers! Can't proceed.", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[Hyprcast] Version mismatch");
    }

    g_hyprcast = std::make_unique<CHyprcast>();

    return {.name{PLUGIN_NAME}, .description{DESCRIPTION}, .author{AUTHOR}, .version{VERSION}};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_hyprcast.reset();
}
