#include "KeyboardRegistry.hpp"

#include "KeyboardInfo.hpp"
#include "hyprcast/config/Config.hpp"
#include "hyprcast/protocol/KeyEvent.hpp"
#include "hyprcast/protocol/KeyState.hpp"
#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/Keymap.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RegistrySnapshot.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <stdexcept>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include <hyprland/src/Compositor.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>

namespace Hyprcast {
    namespace {
        SKeyEvent toHyprcastType(const IKeyboard::SKeyEvent& event) noexcept {
            auto state = event.state == WL_KEYBOARD_KEY_STATE_PRESSED ? eKeyState::PRESSED : eKeyState::RELEASED;

            return {.timeMs = event.timeMs, .keycode = event.keycode, .state = state};
        }

        SModifiers toHyprcastType(const IKeyboard::SModifiersEvent& event) noexcept {
            return {.depressed = event.depressed, .latched = event.latched, .locked = event.locked, .group = event.group};
        }

        Keymap toHyprcastType(const IKeyboard::SKeymapEvent& event) {
            std::unique_ptr<char, decltype(&std::free)> keymapString{::xkb_keymap_get_as_string(event.keymap, XKB_KEYMAP_FORMAT_TEXT_V1), &std::free};
            if (!keymapString) {
                throw std::runtime_error("Cannot serialize XKB keymap");
            }

            return keymapString.get();
        }

        SRepeatInfo toHyprcastType(int repeatRate, int repeatDelay) {
            return {.rate = repeatRate, .delay = repeatDelay};
        }
    }

    CKeyboardRegistry::CKeyboardRegistry(SCallbacks callbacks) : m_callbacks(std::move(callbacks)) {
        for (auto& keyboard : g_pInputManager->m_keyboards) {
            addKeyboard(keyboard);
        }

        // In Lua config reloads, config.reloaded fires before the deferred keyboard
        // refresh. This notification fires after applyConfigToKeyboard sets the keymap.
        m_layoutListener = Event::bus()->m_events.input.keyboard.layout.listen([this](SP<IKeyboard> keyboard, const std::string&) {
            runGuarded("Cannot process keyboard layout update; disconnecting client", [this, &keyboard] { syncKeymap(keyboard); });
        });
    }

    void CKeyboardRegistry::addKeyboard(SP<IKeyboard> keyboard) {
        auto keyboardInfo = std::make_unique<SKeyboardInfo>();

        keyboardInfo->keyboard       = keyboard;
        keyboardInfo->id             = m_nextId++;
        keyboardInfo->name           = keyboard->m_hlName;
        keyboardInfo->pendingRemoval = false;
        keyboardInfo->subscribed     = false;

        keyboardInfo->destroyListener = keyboard->m_events.destroy.listen([this, record = keyboardInfo.get()] { scheduleRemoval(*record); });

        m_keyboardRegistry.push_back(std::move(keyboardInfo));
    }

    void CKeyboardRegistry::updateSubscriptions(const SConfig& acceptedConfig) {
        for (const auto& keyboardInfo : m_keyboardRegistry) {
            if (keyboardInfo->pendingRemoval) {
                continue;
            }

            auto it = std::ranges::find(acceptedConfig.filteredKeyboards, keyboardInfo->name);
            if ((acceptedConfig.filterSetting == eKeyboardFilterSetting::INCLUDE && it == acceptedConfig.filteredKeyboards.end()) ||
                (acceptedConfig.filterSetting == eKeyboardFilterSetting::EXCLUDE && it != acceptedConfig.filteredKeyboards.end())) {
                unsubscribeListeners(*keyboardInfo);
                continue;
            }

            if (keyboardInfo->subscribed) {
                continue;
            }

            const auto keyboard = keyboardInfo->keyboard.lock();
            if (!keyboard) {
                continue;
            }

            KeyboardId keyboardId = keyboardInfo->id;

            keyboardInfo->keyEventListener = keyboard->m_keyboardEvents.key.listen([this, keyboardId](const IKeyboard::SKeyEvent& event) {
                runGuarded("Cannot process key event; disconnecting client", [this, keyboardId, &event] { m_callbacks.onKeyEvent(keyboardId, toHyprcastType(event)); });
            });

            keyboardInfo->modifiersListener = keyboard->m_keyboardEvents.modifiers.listen([this, keyboardId](const IKeyboard::SModifiersEvent& event) {
                runGuarded("Cannot process modifiers update; disconnecting client",
                           [this, keyboardId, &event] { m_callbacks.onModifiersEvent(keyboardId, toHyprcastType(event)); });
            });

            keyboardInfo->keymapListener = keyboard->m_keyboardEvents.keymap.listen([this, keyboardId, keyboardInfo = keyboardInfo.get()](const IKeyboard::SKeymapEvent& event) {
                runGuarded("Cannot process keymap update; disconnecting client", [this, keyboardId, keyboardInfo, &event] {
                    auto keymap                  = toHyprcastType(event);
                    keyboardInfo->lastSentKeymap = keymap;
                    m_callbacks.onKeymapEvent(keyboardId, std::move(keymap));
                });
            });

            keyboardInfo->repeatInfoListener = keyboard->m_keyboardEvents.repeatInfo.listen([this, keyboardId, weakKeyboard = keyboardInfo->keyboard]() {
                auto keyboard = weakKeyboard.lock();
                if (!keyboard) {
                    return;
                }

                runGuarded("Cannot process repeat settings update; disconnecting client",
                           [this, keyboardId, &keyboard] { m_callbacks.onRepeatInfoEvent(keyboardId, toHyprcastType(keyboard->m_repeatRate, keyboard->m_repeatDelay)); });
            });

            keyboardInfo->subscribed = true;
            logMessage(Log::TRACE, "Subscribed to keyboard {} ('{}')", keyboardId, keyboardInfo->name);
            runGuarded("Cannot prepare keyboard subscription; disconnecting client", [this, &keyboard, keyboardId, &keyboardInfo] {
                auto snapshot                = getKeyboardSnapshot(*keyboard, keyboardId, keyboardInfo->name);
                keyboardInfo->lastSentKeymap = snapshot.keymap;

                m_callbacks.onSubscribe(std::move(snapshot));
            });
        }
    }

    RegistrySnapshot CKeyboardRegistry::getRegistrySnapshot() {
        RegistrySnapshot registrySnapshot{};

        for (const auto& keyboardInfo : m_keyboardRegistry) {
            if (!keyboardInfo->subscribed) {
                continue;
            }

            const auto keyboard = keyboardInfo->keyboard.lock();
            if (!keyboard) {
                continue;
            }

            auto snapshot                = getKeyboardSnapshot(*keyboard, keyboardInfo->id, keyboardInfo->name);
            keyboardInfo->lastSentKeymap = snapshot.keymap;
            registrySnapshot.push_back(std::move(snapshot));
        }

        return registrySnapshot;
    }

    void CKeyboardRegistry::shutdown() noexcept {
        m_layoutListener.reset();
        m_wlIdleKeyboardRemoval.reset();

        for (auto& keyboardInfo : m_keyboardRegistry) {
            keyboardInfo->keyEventListener.reset();
            keyboardInfo->modifiersListener.reset();
            keyboardInfo->keymapListener.reset();
            keyboardInfo->repeatInfoListener.reset();
            keyboardInfo->destroyListener.reset();

            keyboardInfo->subscribed = false;
        }
    }

    void CKeyboardRegistry::syncKeymap(const SP<IKeyboard>& keyboard) {
        if (!keyboard) {
            return;
        }

        auto it = std::ranges::find_if(m_keyboardRegistry, [&keyboard](const auto& keyboardInfo) { return keyboardInfo->subscribed && keyboardInfo->keyboard.lock() == keyboard; });
        if (it == m_keyboardRegistry.end()) {
            return;
        }

        auto&       keyboardInfo = **it;
        const auto& activeKeymap = keyboard->m_xkbKeymapV1String;
        if (keyboardInfo.lastSentKeymap == activeKeymap) {
            return;
        }

        m_callbacks.onKeymapEvent(keyboardInfo.id, activeKeymap);
        keyboardInfo.lastSentKeymap = activeKeymap;
    }

    void CKeyboardRegistry::scheduleRemoval(SKeyboardInfo& keyboardInfo) noexcept {
        keyboardInfo.pendingRemoval = true;
        unsubscribeListeners(keyboardInfo);

        if (m_wlIdleKeyboardRemoval) {
            return;
        }

        // If scheduling fails, retain the inactive records and retry on the next scheduleRemoval() call.
        m_wlIdleKeyboardRemoval.reset(::wl_event_loop_add_idle(
            g_pCompositor->m_wlEventLoop,
            [](void* data) {
                auto* self = static_cast<CKeyboardRegistry*>(data);

                // Wayland removes this source after the callback returns; release ownership to avoid double removal.
                [[maybe_unused]] auto* obj = self->m_wlIdleKeyboardRemoval.release();

                std::erase_if(self->m_keyboardRegistry, [](const auto& record) { return record->pendingRemoval; });
            },
            this));
        if (!m_wlIdleKeyboardRemoval) {
            logMessage(Log::WARN, "Cannot schedule keyboard cleanup; retaining inactive records until next removal");
        }
    }

    void CKeyboardRegistry::unsubscribeListeners(SKeyboardInfo& keyboardInfo) {
        if (!keyboardInfo.subscribed) {
            return;
        }

        keyboardInfo.keyEventListener.reset();
        keyboardInfo.modifiersListener.reset();
        keyboardInfo.keymapListener.reset();
        keyboardInfo.repeatInfoListener.reset();

        keyboardInfo.subscribed = false;
        logMessage(Log::TRACE, "Unsubscribed from keyboard {} ('{}')", keyboardInfo.id, keyboardInfo.name);
        runGuarded("Cannot prepare keyboard unsubscription; disconnecting client", [this, &keyboardInfo] { m_callbacks.onUnsubscribe(keyboardInfo.id); });
    }

    SKeyboardSnapshot CKeyboardRegistry::getKeyboardSnapshot(const IKeyboard& keyboard, KeyboardId id, const std::string& name) {
        return {
            .id   = id,
            .name = name,
            .modifiers =
                {
                    .depressed = keyboard.m_modifiersState.depressed,
                    .latched   = keyboard.m_modifiersState.latched,
                    .locked    = keyboard.m_modifiersState.locked,
                    .group     = keyboard.m_modifiersState.group,
                },
            // Keep subscription snapshots consistent with incremental keymap events and
            // interoperable with Wayland clients: both use libxkbcommon text V1.
            .keymap = keyboard.m_xkbKeymapV1String,
            .repeatInfo =
                {
                    .rate  = keyboard.m_repeatRate,
                    .delay = keyboard.m_repeatDelay,
                },
        };
    }
}
