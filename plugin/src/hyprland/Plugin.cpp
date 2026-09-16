#include "Plugin.hpp"

#include "PluginConfig.hpp"
#include "KeyboardRegistry.hpp"

#include <helpers/memory/Memory.hpp>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
    class CPluginState {
      public:
        CPluginState() {
            m_config.listen([this] { m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig()); });
            m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig());
        };
        ~CPluginState() = default;

        CPluginState(const CPluginState& other)            = delete;
        CPluginState& operator=(const CPluginState& other) = delete;
        CPluginState(CPluginState&& other)                 = delete;
        CPluginState& operator=(CPluginState&& other)      = delete;

        void          addKeyboard(SP<IKeyboard> keyboard) {
            m_keyboardRegistry.addKeyboard(keyboard);
            m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig());
        }

      private:
        Hyprcast::CKeyboardRegistry m_keyboardRegistry;
        Hyprcast::CPluginConfig     m_config;
    };

    std::unique_ptr<CPluginState> g_pPluginState;

    CFunctionHook*                g_pSetupKeyboardHook = nullptr;
    using ogSetupKeyboard                              = void (*)(void*, SP<IKeyboard>);

    void setupKeyboardHook(void* thisPtr, SP<IKeyboard> keyboard) {
        (*(ogSetupKeyboard)g_pSetupKeyboardHook->m_original)(thisPtr, keyboard);
        g_pPluginState->addKeyboard(keyboard);
    }
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

    g_pPluginState = std::make_unique<CPluginState>();

    static const auto METHODS = HyprlandAPI::findFunctionsByName(Hyprcast::PHANDLE, "setupKeyboard");
    g_pSetupKeyboardHook      = HyprlandAPI::createFunctionHook(handle, METHODS[0].address, (void*)&setupKeyboardHook);
    g_pSetupKeyboardHook->hook();

    return {.name{Hyprcast::PLUGIN_NAME}, .description{Hyprcast::DESCRIPTION}, .author{Hyprcast::AUTHOR}, .version{Hyprcast::VERSION}};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_pPluginState.reset();
}
