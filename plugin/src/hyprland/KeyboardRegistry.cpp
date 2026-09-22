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

#include <hyprland/src/managers/input/InputManager.hpp>
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

            keyboardInfo->keyEventListener =
                keyboard->m_keyboardEvents.key.listen([this, keyboardId](const IKeyboard::SKeyEvent& event) { m_callbacks.handleKeyEvent(keyboardId, toHyprcastType(event)); });

            keyboardInfo->modifiersListener = keyboard->m_keyboardEvents.modifiers.listen(
                [this, keyboardId](const IKeyboard::SModifiersEvent& event) { m_callbacks.handleModifiers(keyboardId, toHyprcastType(event)); });

            keyboardInfo->keymapListener =
                keyboard->m_keyboardEvents.keymap.listen([this, keyboardId](const IKeyboard::SKeymapEvent& event) { m_callbacks.handleKeymap(keyboardId, toHyprcastType(event)); });

            keyboardInfo->repeatInfoListener = keyboard->m_keyboardEvents.repeatInfo.listen([this, keyboardId, weakKeyboard = keyboardInfo->keyboard]() {
                auto keyboard = weakKeyboard.lock();
                if (!keyboard) {
                    return;
                }

                m_callbacks.handleRepeatInfo(keyboardId, toHyprcastType(keyboard->m_repeatRate, keyboard->m_repeatDelay));
            });

            keyboardInfo->subscribed = true;
            m_callbacks.handleSubscription(getKeyboardSnapshot(*keyboard, keyboardId, keyboardInfo->name));
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

            registrySnapshot.push_back(getKeyboardSnapshot(*keyboard, keyboardInfo->id, keyboardInfo->name));
        }

        return registrySnapshot;
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
        m_callbacks.handleUnsubscription(keyboardInfo.id);
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
            .keymap = keyboard.m_xkbKeymapString,
            .repeatInfo =
                {
                    .rate  = keyboard.m_repeatRate,
                    .delay = keyboard.m_repeatDelay,
                },
        };
    }
}
