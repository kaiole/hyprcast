#include "Hyprcast.hpp"

#include "CallbackBoundary.hpp"
#include "hyprcast/protocol/KeyEvent.hpp"
#include "hyprcast/protocol/KeyState.hpp"
#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/KeyboardSnapshot.hpp"
#include "hyprcast/protocol/Keymap.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"
#include "hyprcast/protocol/Wire.hpp"
#include "hyprland/KeyboardRegistry.hpp"
#include "hyprland/PluginConfig.hpp"
#include "libjson/json_value.hpp"
#include "libjson/serializer.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Hyprcast {
    namespace {
        using namespace Wire;

        [[nodiscard]] std::string key(std::string_view name) {
            return std::string{name};
        }

        [[nodiscard]] libjson::json_value::object toJsonObject(const SKeyEvent& keyEvent) {
            return libjson::json_value::object{
                {key(FIELD_TIME_MS), {.data = static_cast<std::uint64_t>(keyEvent.timeMs)}},
                {key(FIELD_KEYCODE), {.data = static_cast<std::uint64_t>(keyEvent.keycode)}},
                {key(FIELD_STATE), {.data = key(keyEvent.state == eKeyState::PRESSED ? STATE_PRESSED : STATE_RELEASED)}},
            };
        }

        [[nodiscard]] libjson::json_value::object toJsonObject(const SModifiers& modifiers) {
            return libjson::json_value::object{
                {key(FIELD_DEPRESSED), {.data = static_cast<std::uint64_t>(modifiers.depressed)}},
                {key(FIELD_LATCHED), {.data = static_cast<std::uint64_t>(modifiers.latched)}},
                {key(FIELD_LOCKED), {.data = static_cast<std::uint64_t>(modifiers.locked)}},
                {key(FIELD_GROUP), {.data = static_cast<std::uint64_t>(modifiers.group)}},
            };
        }

        [[nodiscard]] libjson::json_value::object toJsonObject(const SRepeatInfo& repeatInfo) {
            return libjson::json_value::object{
                {key(FIELD_RATE), {.data = static_cast<std::uint64_t>(repeatInfo.rate)}},
                {key(FIELD_DELAY), {.data = static_cast<std::uint64_t>(repeatInfo.delay)}},
            };
        }

        [[nodiscard]] libjson::json_value::object toJsonObject(SKeyboardSnapshot snapshot) {
            auto jsonObject = toJsonObject(snapshot.modifiers);
            jsonObject.merge(toJsonObject(snapshot.repeatInfo));

            jsonObject.emplace(key(FIELD_NAME), libjson::json_value{.data = std::move(snapshot.name)});
            jsonObject.emplace(key(FIELD_KEYMAP), libjson::json_value{.data = std::move(snapshot.keymap)});

            return jsonObject;
        }

        [[nodiscard]] libjson::json_value makeEvent(std::string_view eventName, KeyboardId id, libjson::json_value::object jsonObject) {
            jsonObject.emplace(key(FIELD_EVENT), libjson::json_value{.data = key(eventName)});
            jsonObject.emplace(key(FIELD_KEYBOARD_ID), libjson::json_value{.data = static_cast<std::uint64_t>(id)});

            return {.data = std::move(jsonObject)};
        }

        [[nodiscard]] std::string keyMessage(KeyboardId id, const SKeyEvent& keyEvent) {
            return libjson::serialize(makeEvent(EVENT_KEY, id, toJsonObject(keyEvent)));
        }

        [[nodiscard]] std::string modifiersMessage(KeyboardId id, const SModifiers& modifiers) {
            return libjson::serialize(makeEvent(EVENT_MODIFIERS, id, toJsonObject(modifiers)));
        }

        [[nodiscard]] std::string keymapMessage(KeyboardId id, Keymap keymap) {
            libjson::json_value::object jsonObject;
            jsonObject.emplace(key(FIELD_KEYMAP), libjson::json_value{.data = std::move(keymap)});

            return libjson::serialize(makeEvent(EVENT_KEYMAP, id, std::move(jsonObject)));
        }

        [[nodiscard]] std::string repeatInfoMessage(KeyboardId id, const SRepeatInfo& repeatInfo) {
            return libjson::serialize(makeEvent(EVENT_REPEAT_INFO, id, toJsonObject(repeatInfo)));
        }

        [[nodiscard]] std::string subscribeMessage(SKeyboardSnapshot snapshot) {
            const auto id = snapshot.id;
            return libjson::serialize(makeEvent(EVENT_SUBSCRIBE_KEYBOARD, id, toJsonObject(std::move(snapshot))));
        }

        [[nodiscard]] std::string unsubscribeMessage(KeyboardId id) {
            return libjson::serialize(makeEvent(EVENT_UNSUBSCRIBE_KEYBOARD, id, libjson::json_value::object{}));
        }

        [[nodiscard]] std::string castingStateMessage(bool isPaused) {
            return libjson::serialize(makeEvent(EVENT_CASTING_STATE, GLOBAL_KEYBOARD_ID, libjson::json_value::object{{key(FIELD_PAUSED), {.data = isPaused}}}));
        }
    }

    // Capture starts paused and pauses on disconnect, so only an explicit client request enables it.
    CHyprcast::CHyprcast() :
        m_server([this] { requestRegistrySnapshot(); }, [this](bool enabled) { setPause(!enabled); }, [this] { m_paused = true; }), m_keyboardRegistry(makeRegistryCallbacks()),
        m_config(makePluginConfigCallbacks()) {
        m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig());
    }

    void CHyprcast::addKeyboard(const SP<IKeyboard>& keyboard) {
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

    void CHyprcast::setPause(bool paused) {
        if (m_shutdown) {
            throw std::runtime_error("Hyprcast is stopped; reload the plugin");
        }

        // Always echo the authoritative state, including idempotent control requests.
        m_paused = paused;
        m_server.queueMessage(castingStateMessage(m_paused));
    }

    bool CHyprcast::isPaused() const noexcept {
        return m_paused;
    }

    CPluginConfig::SCallbacks CHyprcast::makePluginConfigCallbacks() {
        return {
            .onConfigReload =
                [this] {
                    if (m_shutdown) {
                        return;
                    }

                    m_keyboardRegistry.updateSubscriptions(m_config.getAcceptedConfig());
                },
            .onException = [this] noexcept { shutdown(); },
        };
    }

    void CHyprcast::requestRegistrySnapshot() {
        m_server.queueMessage(castingStateMessage(m_paused));

        auto registrySnapshot = m_keyboardRegistry.getRegistrySnapshot();

        for (auto& keyboardSnapshot : registrySnapshot) {
            m_server.queueMessage(subscribeMessage(std::move(keyboardSnapshot)));
        }
    }

    CKeyboardRegistry::SCallbacks CHyprcast::makeRegistryCallbacks() {
        return {
            .onException   = [this] noexcept { m_server.disconnectClient(); },
            .onSubscribe   = [this](SKeyboardSnapshot keyboardSnapshot) { m_server.queueMessage(subscribeMessage(std::move(keyboardSnapshot))); },
            .onUnsubscribe = [this](KeyboardId id) { m_server.queueMessage(unsubscribeMessage(id)); },
            .onKeyEvent =
                [this](KeyboardId id, SKeyEvent keyEvent) {
                    if (!m_paused) {
                        m_server.queueMessage(keyMessage(id, keyEvent));
                    }
                },
            .onModifiersEvent  = [this](KeyboardId id, SModifiers modifiers) { m_server.queueMessage(modifiersMessage(id, modifiers)); },
            .onKeymapEvent     = [this](KeyboardId id, Keymap keymap) { m_server.queueMessage(keymapMessage(id, std::move(keymap))); },
            .onRepeatInfoEvent = [this](KeyboardId id, SRepeatInfo repeatInfo) { m_server.queueMessage(repeatInfoMessage(id, repeatInfo)); },
        };
    }

    void CHyprcast::shutdown() noexcept {
        if (m_shutdown) {
            return;
        }
        notifyFailure("Plugin stopped after an internal failure. Unload and reload hyprcast; see Hyprland logs for details.");
        m_keyboardRegistry.shutdown();
        m_server.shutdown();
        m_shutdown = true;
    }
}
