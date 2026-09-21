#include "Hyprcast.hpp"

#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/Keymap.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"
#include "hyprland/KeyboardRegistry.hpp"
#include "libjson/json_value.hpp"
#include "libjson/serializer.hpp"
#include <cstdint>

namespace Hyprcast {
    namespace {
        [[nodiscard]] libjson::json_value toJsonValue(KeyboardId id, const SModifiers& modifiers) {
            return {.data = libjson::json_value::object{
                        {"event", {.data = "modifiers"}},
                        {"keyboard_id", {.data = static_cast<std::uint64_t>(id)}},
                        {"depressed", {.data = static_cast<std::uint64_t>(modifiers.depressed)}},
                        {"latched", {.data = static_cast<std::uint64_t>(modifiers.latched)}},
                        {"locked", {.data = static_cast<std::uint64_t>(modifiers.locked)}},
                        {"group", {.data = static_cast<std::uint64_t>(modifiers.group)}},
                    }};
        }

        [[nodiscard]] libjson::json_value toJsonValue(KeyboardId id, const Keymap& keymap) {
            return {.data = libjson::json_value::object{{"event", {.data = "keymap"}}, {"keyboard_id", {.data = static_cast<std::uint64_t>(id)}}, {"keymap", {.data = keymap}}}};
        }

        [[nodiscard]] libjson::json_value toJsonValue(KeyboardId id, const SRepeatInfo& repeatInfo) {
            return {.data = libjson::json_value::object{
                        {"event", {.data = "modifiers"}},
                        {"keyboard_id", {.data = static_cast<std::uint64_t>(id)}},
                        {"rate", {.data = static_cast<std::uint64_t>(repeatInfo.rate)}},
                        {"delay", {.data = static_cast<std::uint64_t>(repeatInfo.delay)}},
                    }};
        }
    }

    CHyprcast::CHyprcast() : m_keyboardRegistry(makeRegistryCallbacks()) {
        m_config.listen([this] { m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig()); });
        m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig());
    };

    void CHyprcast::addKeyboard(SP<IKeyboard> keyboard) {
        m_keyboardRegistry.addKeyboard(keyboard);
        m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig());
    }

    CKeyboardRegistry::SCallbacks CHyprcast::makeRegistryCallbacks() {
        return {.handleModifiers  = [this](KeyboardId id, const SModifiers& modifiers) { m_socket.queueMessage(libjson::serialize(toJsonValue(id, modifiers))); },
                .handleKeymap     = [this](KeyboardId id, const Keymap& keymap) { m_socket.queueMessage(libjson::serialize(toJsonValue(id, keymap))); },
                .handleRepeatInfo = [this](KeyboardId id, const SRepeatInfo& repeatInfo) { m_socket.queueMessage(libjson::serialize(toJsonValue(id, repeatInfo))); }};
    }
}
