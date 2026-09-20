#include "Hyprcast.hpp"

#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/Keymap.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"
#include "hyprland/KeyboardRegistry.hpp"

namespace Hyprcast {
    CHyprcast::CHyprcast() : m_keyboardRegistry(makeRegistryCallbacks()) {
        m_config.listen([this] { m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig()); });
        m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig());
    };

    void CHyprcast::addKeyboard(SP<IKeyboard> keyboard) {
        m_keyboardRegistry.addKeyboard(keyboard);
        m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig());
    }

    CKeyboardRegistry::SCallbacks CHyprcast::makeRegistryCallbacks() {
        return {.modifiersEvent =
                    [this](KeyboardId id, const SModifiers& modifiers) {
                        // send modifiers event
                    },
                .keymapEvent =
                    [this](KeyboardId id, const Keymap& keymap) {
                        // send keymap event
                    },
                .repeatInfoEvent =
                    [this](KeyboardId id, const SRepeatInfo& repeatInfo) {
                        // send repeat info event
                    }};
    }
}
