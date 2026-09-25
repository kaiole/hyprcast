#include "Plugin.hpp"

#include "Hyprcast.hpp"
#include "Log.hpp"

#include <exception>
#include <helpers/memory/Memory.hpp>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
    std::unique_ptr<Hyprcast::CHyprcast> g_pHyprcast;

    CFunctionHook*                       g_pSetupKeyboardHook = nullptr;
    using ogSetupKeyboard                                     = void (*)(void*, SP<IKeyboard>);

    void setupKeyboardHook(void* thisPtr, SP<IKeyboard> keyboard) {
        (*(ogSetupKeyboard)g_pSetupKeyboardHook->m_original)(thisPtr, keyboard);
        g_pHyprcast->addKeyboard(keyboard);
    }

    int toggleCasting(lua_State*) noexcept {
        try {
            if (!g_pHyprcast) {
                return 0;
            }

            g_pHyprcast->setPause(!g_pHyprcast->isPaused());
        } catch (const std::exception& error) {
            Hyprcast::logError("Cannot toggle casting: {}", error.what());
            Hyprcast::notifyFailure("Cannot toggle casting; see Hyprland logs.");
        } catch (...) {
            Hyprcast::logError("Cannot toggle casting: unknown exception");
            Hyprcast::notifyFailure("Cannot toggle casting; see Hyprland logs.");
        }

        return 0;
    }

}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) try {
    Hyprcast::PHANDLE = handle;

    const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
    const std::string CLIENT_HASH     = __hyprland_api_get_client_hash();

    if (COMPOSITOR_HASH != CLIENT_HASH) {
        throw std::runtime_error("Hyprland version mismatch: rebuild hyprcast against the running Hyprland version");
    }

    g_pHyprcast = std::make_unique<Hyprcast::CHyprcast>();

    static const auto METHODS = HyprlandAPI::findFunctionsByName(Hyprcast::PHANDLE, "setupKeyboard");
    if (METHODS.empty()) {
        throw std::runtime_error("Cannot find Hyprland setupKeyboard function");
    }

    g_pSetupKeyboardHook = HyprlandAPI::createFunctionHook(handle, METHODS[0].address, (void*)&setupKeyboardHook);
    if (!g_pSetupKeyboardHook || !g_pSetupKeyboardHook->hook()) {
        throw std::runtime_error("Cannot install Hyprland setupKeyboard hook");
    }

    if (!HyprlandAPI::addLuaFunction(handle, "hyprcast", "toggle", &toggleCasting)) {
        throw std::runtime_error("Cannot register Lua function hyprcast.toggle");
    }

    Hyprcast::logMessage(Log::TRACE, "Plugin loaded");

    return {.name{Hyprcast::PLUGIN_NAME}, .description{Hyprcast::DESCRIPTION}, .author{Hyprcast::AUTHOR}, .version{Hyprcast::VERSION}};
} catch (const std::exception& error) {
    Hyprcast::logError("Cannot load plugin: {}", error.what());
    Hyprcast::notifyFailure(error.what());
    g_pHyprcast.reset();
    throw;
} catch (...) {
    Hyprcast::logError("Cannot load plugin: unknown exception");
    Hyprcast::notifyFailure("Cannot load plugin; see Hyprland logs for details.");
    g_pHyprcast.reset();
    throw;
}

APICALL EXPORT void PLUGIN_EXIT() {
    HyprlandAPI::removeDispatcher(Hyprcast::PHANDLE, "hyprcast");
    g_pHyprcast.reset();
    Hyprcast::logMessage(Log::TRACE, "Plugin unloaded");
}
