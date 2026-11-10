#include "Plugin.hpp"

#include "Hyprcast.hpp"
#include "Log.hpp"

#include <helpers/memory/Memory.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

// Hyprland calls the entry points and the hook below as plain functions, so
// the state they share is global. teardown() empties both before the plugin
// is unloaded, which leaves the unique_ptr destructor nothing to do.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTNEXTLINE(clang-diagnostic-global-constructors)
std::unique_ptr<hyprcast::keycaster> keycaster_instance;

CFunctionHook* setup_keyboard_hook = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
using setup_keyboard_fn = void (*)(void*, SP<IKeyboard>);

// The signature has to match the hooked CInputManager::setupKeyboard().
// NOLINTNEXTLINE(performance-unnecessary-value-param)
void on_setup_keyboard(void* this_ptr, SP<IKeyboard> keyboard) {
    // The hook API hands the original function back as a void*.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<setup_keyboard_fn>(
        setup_keyboard_hook->m_original)(this_ptr, keyboard);
    if (keycaster_instance) {
        keycaster_instance->add_keyboard(keyboard);
    }
}

int toggle_casting(lua_State*) noexcept {
    try {
        if (!keycaster_instance) {
            return 0;
        }

        keycaster_instance->set_pause(!keycaster_instance->is_paused());
    } catch (const std::exception& error) {
        hyprcast::log_error("Cannot toggle casting: {}", error.what());
        hyprcast::notify_failure("Cannot toggle casting; see Hyprland logs.");
    } catch (...) {
        hyprcast::log_error("Cannot toggle casting: unknown exception");
        hyprcast::notify_failure("Cannot toggle casting; see Hyprland logs.");
    }

    return 0;
}

// Removes every entry point before destroying the state they use. Hyprland also
// cleans these up on unload, but doing it here keeps the teardown order
// explicit.
void teardown() noexcept {
    if (setup_keyboard_hook) {
        HyprlandAPI::removeFunctionHook(hyprcast::plugin_handle,
                                        setup_keyboard_hook);
        setup_keyboard_hook = nullptr;
    }
    HyprlandAPI::removeLuaFunction(hyprcast::plugin_handle,
                                   "hyprcast",
                                   "toggle");
    keycaster_instance.reset();
}

} // namespace

// Hyprland's plugin ABI declares these two entry points extern "C" yet has
// them return C++ types; the signatures are not ours to change.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#endif

// Do NOT change this function; Hyprland's ABI fixes its signature.
// NOLINTNEXTLINE(modernize-use-string-view)
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) try {
    hyprcast::plugin_handle = handle;

    const std::string compositor_hash = __hyprland_api_get_hash();
    const std::string client_hash = __hyprland_api_get_client_hash();

    if (compositor_hash != client_hash) {
        throw std::runtime_error("Hyprland version mismatch: rebuild hyprcast "
                                 "against the running Hyprland version");
    }

    keycaster_instance = std::make_unique<hyprcast::keycaster>();

    const auto methods =
        HyprlandAPI::findFunctionsByName(hyprcast::plugin_handle,
                                         "setupKeyboard");
    if (methods.empty()) {
        throw std::runtime_error("Cannot find Hyprland setupKeyboard function");
    }

    setup_keyboard_hook = HyprlandAPI::createFunctionHook(
        handle,
        methods.front().address,
        // The hook API takes the replacement function as a void*.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        reinterpret_cast<void*>(&on_setup_keyboard));
    if (!setup_keyboard_hook || !setup_keyboard_hook->hook()) {
        throw std::runtime_error("Cannot install Hyprland setupKeyboard hook");
    }

    if (!HyprlandAPI::addLuaFunction(handle,
                                     "hyprcast",
                                     "toggle",
                                     &toggle_casting)) {
        throw std::runtime_error(
            "Cannot register Lua function hyprcast.toggle");
    }

    hyprcast::log_message(Log::TRACE, "Plugin loaded");

    return {
        .name {hyprcast::plugin_name},
        .description {hyprcast::description},
        .author {hyprcast::author},
        .version {hyprcast::version},
    };
} catch (const std::exception& error) {
    hyprcast::log_error("Cannot load plugin: {}", error.what());
    hyprcast::notify_failure(error.what());
    teardown();
    throw;
} catch (...) {
    hyprcast::log_error("Cannot load plugin: unknown exception");
    hyprcast::notify_failure(
        "Cannot load plugin; see Hyprland logs for details.");
    teardown();
    throw;
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

APICALL EXPORT void PLUGIN_EXIT() {
    teardown();
    hyprcast::log_message(Log::TRACE, "Plugin unloaded");
}
