#include "Hyprcast.hpp"

#include "CallbackBoundary.hpp"
#include "hyprcast/protocol/KeyEvent.hpp"
#include "hyprcast/protocol/KeyState.hpp"
#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/KeyboardSnapshot.hpp"
#include "hyprcast/protocol/Keymap.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"
#include "hyprland/KeyboardRegistry.hpp"
#include "hyprland/PluginConfig.hpp"
#include "libjson/json_value.hpp"
#include "libjson/serializer.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Hyprcast {
    namespace {
        [[nodiscard]] libjson::json_value::object toJsonObject(const SKeyEvent& keyEvent) {
            return libjson::json_value::object{
                {"time_ms", {.data = static_cast<std::uint64_t>(keyEvent.timeMs)}},
                {"keycode", {.data = static_cast<std::uint64_t>(keyEvent.keycode)}},
                {"state", {.data = std::string{keyEvent.state == eKeyState::PRESSED ? "pressed" : "released"}}},
            };
        }

        [[nodiscard]] libjson::json_value::object toJsonObject(const SModifiers& modifiers) {
            return libjson::json_value::object{
                {"depressed", {.data = static_cast<std::uint64_t>(modifiers.depressed)}},
                {"latched", {.data = static_cast<std::uint64_t>(modifiers.latched)}},
                {"locked", {.data = static_cast<std::uint64_t>(modifiers.locked)}},
                {"group", {.data = static_cast<std::uint64_t>(modifiers.group)}},
            };
        }

        [[nodiscard]] libjson::json_value::object toJsonObject(const SRepeatInfo& repeatInfo) {
            return libjson::json_value::object{
                {"rate", {.data = static_cast<std::uint64_t>(repeatInfo.rate)}},
                {"delay", {.data = static_cast<std::uint64_t>(repeatInfo.delay)}},
            };
        }

        [[nodiscard]] libjson::json_value::object toJsonObject(SKeyboardSnapshot snapshot) {
            auto jsonObject = toJsonObject(snapshot.modifiers);
            jsonObject.merge(toJsonObject(snapshot.repeatInfo));

            jsonObject.emplace("name", libjson::json_value{.data = std::move(snapshot.name)});
            jsonObject.emplace("keymap", libjson::json_value{.data = std::move(snapshot.keymap)});

            return jsonObject;
        }

        [[nodiscard]] libjson::json_value makeEvent(std::string_view eventName, KeyboardId id, libjson::json_value::object jsonObject) {
            jsonObject.emplace("event", libjson::json_value{.data = std::string{eventName}});
            jsonObject.emplace("keyboard_id", libjson::json_value{.data = static_cast<std::uint64_t>(id)});

            return {.data = std::move(jsonObject)};
        }

        [[nodiscard]] std::string createMessage(KeyboardId id, const SKeyEvent& keyEvent) {
            return libjson::serialize(makeEvent("key", id, toJsonObject(keyEvent)));
        }

        [[nodiscard]] std::string createMessage(KeyboardId id, const SModifiers& modifiers) {
            return libjson::serialize(makeEvent("modifiers", id, toJsonObject(modifiers)));
        }

        [[nodiscard]] std::string createMessage(KeyboardId id, Keymap keymap) {
            libjson::json_value::object jsonObject;
            jsonObject.emplace("keymap", libjson::json_value{.data = std::move(keymap)});

            return libjson::serialize(makeEvent("keymap", id, std::move(jsonObject)));
        }

        [[nodiscard]] std::string createMessage(KeyboardId id, const SRepeatInfo& repeatInfo) {
            return libjson::serialize(makeEvent("repeat_info", id, toJsonObject(repeatInfo)));
        }

        [[nodiscard]] std::string createMessage(SKeyboardSnapshot snapshot) {
            const auto id = snapshot.id;
            return libjson::serialize(makeEvent("subscribe_keyboard", id, toJsonObject(std::move(snapshot))));
        }

        [[nodiscard]] std::string createMessage(KeyboardId id) {
            return libjson::serialize(makeEvent("unsubscribe_keyboard", id, libjson::json_value::object{}));
        }

        [[nodiscard]] std::string createMessage(bool isPaused) {
            // KeyboardId's begin at 1.
            constexpr int GLOBAL_EVENT_ID = 0;
            return libjson::serialize(makeEvent("casting_state", GLOBAL_EVENT_ID, libjson::json_value::object{{"paused", {.data = isPaused}}}));
        }
    }

    CHyprcast::CHyprcast() :
        m_socket([this] { requestRegistrySnapshot(); }, [this](bool enabled) { setPause(!enabled); }, [this] { m_paused = true; }), m_keyboardRegistry(makeRegistryCallbacks()),
        m_config(makePluginConfigCallbacks()) {
        m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig());
    };

    void CHyprcast::addKeyboard(SP<IKeyboard> keyboard) {
        if (m_shutdown) {
            return;
        }

        runGuarded(
            "Cannot register keyboard; shutting down plugin",
            [this, &keyboard] {
                m_keyboardRegistry.addKeyboard(keyboard);
                m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig());
            },
            [this] noexcept { shutdown(); });
    }

    void CHyprcast::setPause(bool setPause) {
        if (m_shutdown) {
            throw std::runtime_error("Hyprcast is stopped; reload the plugin");
        }

        // Always echo the authoritative state, including idempotent control requests.
        m_paused = setPause;
        m_socket.queueMessage(createMessage(m_paused));
    }

    bool CHyprcast::isPaused() const noexcept {
        return m_paused;
    }

    CPluginConfig::SCallbacks CHyprcast::makePluginConfigCallbacks() {
        return {.onConfigReload =
                    [this] {
                        if (m_shutdown) {
                            return;
                        }

                        m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig());
                    },
                .onException = [this]() noexcept { shutdown(); }};
    }

    void CHyprcast::requestRegistrySnapshot() {
        m_socket.queueMessage(createMessage(m_paused));

        auto registrySnapshot = m_keyboardRegistry.getRegistrySnapshot();

        for (auto& keyboardSnapshot : registrySnapshot) {
            m_socket.queueMessage(createMessage(std::move(keyboardSnapshot)));
        }
    }

    CKeyboardRegistry::SCallbacks CHyprcast::makeRegistryCallbacks() {
        return {.onException   = [this] noexcept { m_socket.disconnectClient(); },
                .onSubscribe   = [this](SKeyboardSnapshot keyboardSnapshot) { m_socket.queueMessage(createMessage(std::move(keyboardSnapshot))); },
                .onUnsubscribe = [this](KeyboardId id) { m_socket.queueMessage(createMessage(id)); },
                .onKeyEvent =
                    [this](KeyboardId id, SKeyEvent keyEvent) {
                        if (!m_paused) {
                            m_socket.queueMessage(createMessage(id, keyEvent));
                        }
                    },
                .onModifiersEvent  = [this](KeyboardId id, SModifiers modifiers) { m_socket.queueMessage(createMessage(id, modifiers)); },
                .onKeymapEvent     = [this](KeyboardId id, Keymap keymap) { m_socket.queueMessage(createMessage(id, std::move(keymap))); },
                .onRepeatInfoEvent = [this](KeyboardId id, SRepeatInfo repeatInfo) { m_socket.queueMessage(createMessage(id, repeatInfo)); }};
    }

    void CHyprcast::shutdown() noexcept {
        if (m_shutdown) {
            return;
        }
        notifyFailure("Plugin stopped after an internal failure. Unload and reload hyprcast; see Hyprland logs for details.");
        m_keyboardRegistry.shutdown();
        m_socket.shutdown();
        m_shutdown = true;
    }
}
