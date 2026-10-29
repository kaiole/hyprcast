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
        reinterpret_cast<ogSetupKeyboard>(g_pSetupKeyboardHook->m_original)(thisPtr, keyboard);
        if (g_pHyprcast) {
            g_pHyprcast->addKeyboard(keyboard);
        }
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

    // Removes every entry point before destroying the state they use. Hyprland also
    // cleans these up on unload, but doing it here keeps the teardown order explicit.
    void teardown() noexcept {
        if (g_pSetupKeyboardHook) {
            HyprlandAPI::removeFunctionHook(Hyprcast::PHANDLE, g_pSetupKeyboardHook);
            g_pSetupKeyboardHook = nullptr;
        }
        HyprlandAPI::removeLuaFunction(Hyprcast::PHANDLE, "hyprcast", "toggle");
        g_pHyprcast.reset();
    }
}

// Do NOT change this function; Hyprland's ABI fixes its signature.
// NOLINTNEXTLINE(modernize-use-string-view)
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

    const auto methods = HyprlandAPI::findFunctionsByName(Hyprcast::PHANDLE, "setupKeyboard");
    if (methods.empty()) {
        throw std::runtime_error("Cannot find Hyprland setupKeyboard function");
    }

    g_pSetupKeyboardHook = HyprlandAPI::createFunctionHook(handle, methods.front().address, reinterpret_cast<void*>(&setupKeyboardHook));
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
    teardown();
    throw;
} catch (...) {
    Hyprcast::logError("Cannot load plugin: unknown exception");
    Hyprcast::notifyFailure("Cannot load plugin; see Hyprland logs for details.");
    teardown();
    throw;
}

APICALL EXPORT void PLUGIN_EXIT() {
    teardown();
    Hyprcast::logMessage(Log::TRACE, "Plugin unloaded");
}
