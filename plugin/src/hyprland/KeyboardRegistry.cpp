#include "KeyboardRegistry.hpp"

#include "KeyboardInfo.hpp"
#include "hyprcast/config/Config.hpp"
#include "hyprcast/protocol/KeyEvent.hpp"
#include "hyprcast/protocol/KeyState.hpp"
#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/Keymap.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"

#include <hyprland/src/managers/input/InputManager.hpp>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include <hyprland/src/Compositor.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <print>
#include <utility>

namespace Hyprcast {
    namespace {
        SKeyEvent toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeyEvent& event) noexcept {
            auto state = event.state == WL_KEYBOARD_KEY_STATE_PRESSED ? eKeyState::PRESSED : eKeyState::RELEASED;

            return {.keyboardId = keyboardId, .timeMs = event.timeMs, .keycode = event.keycode, .state = state};
        }

        SModifiers toHyprcastType(KeyboardId keyboardId, const IKeyboard::SModifiersEvent& event) noexcept {
            return {.keyboardId = keyboardId, .depressed = event.depressed, .latched = event.latched, .locked = event.locked, .group = event.group};
        }

        SKeymap toHyprcastType(KeyboardId keyboardId, const IKeyboard::SKeymapEvent& event) {
            std::unique_ptr<char, decltype(&std::free)> keymapString{::xkb_keymap_get_as_string(event.keymap, XKB_KEYMAP_FORMAT_TEXT_V1), &std::free};

            return {.keyboardId = keyboardId, .keymap = keymapString.get()};
        }

        SRepeatInfo toHyprcastType(KeyboardId keyboardId, int repeatRate, int repeatDelay) {
            return {.keyboardId = keyboardId, .rate = repeatRate, .delay = repeatDelay};
        }
    }

    CKeyboardRegistry::CKeyboardRegistry() {
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
        for (const auto& keyboardPtr : m_keyboardRegistry) {
            if (keyboardPtr->pendingRemoval) {
                continue;
            }

            auto it = std::ranges::find(acceptedConfig.filteredKeyboards, keyboardPtr->name);
            if ((acceptedConfig.filter == eKeyboardFilter::INCLUDE && it == acceptedConfig.filteredKeyboards.end()) ||
                (acceptedConfig.filter == eKeyboardFilter::EXCLUDE && it != acceptedConfig.filteredKeyboards.end())) {
                unsubscribeListeners(*keyboardPtr);
                continue;
            }

            if (keyboardPtr->subscribed) {
                continue;
            }

            const auto& keyboardInfo = keyboardPtr.get();
            const auto& keyboard     = keyboardInfo->keyboard;
            KeyboardId  keyboardId   = keyboardInfo->id;

            keyboardInfo->keyEventListener = keyboard->m_keyboardEvents.key.listen([this, keyboardId](const IKeyboard::SKeyEvent& event) {
                auto keyEventInfo = toHyprcastType(keyboardId, event);
                std::println(stderr, "[hyprcast] keycode: {} {} at {} by keyboard {}", keyEventInfo.keycode, keyEventInfo.state == eKeyState::PRESSED ? "pressed" : "released",
                             keyEventInfo.timeMs, keyboardId);

                // TODO: queue IPC message
            });

            keyboardInfo->modifiersListener = keyboard->m_keyboardEvents.modifiers.listen([this, keyboardId](const IKeyboard::SModifiersEvent& event) {
                auto modifiersInfo = toHyprcastType(keyboardId, event);

                // TODO: queue IPC message
            });

            keyboardInfo->keymapListener = keyboard->m_keyboardEvents.keymap.listen([this, keyboardId](const IKeyboard::SKeymapEvent& event) {
                auto keymapInfo = toHyprcastType(keyboardId, event);

                // TODO: queue IPC message
            });

            keyboardInfo->repeatInfoListener = keyboard->m_keyboardEvents.repeatInfo.listen([this, keyboardId, weakKeyboard = keyboardInfo->keyboard]() {
                auto liveKeyboard = weakKeyboard.lock();
                if (!liveKeyboard) {
                    return;
                }

                auto repeatInfo = toHyprcastType(keyboardId, liveKeyboard->m_repeatRate, liveKeyboard->m_repeatDelay);

                // TODO: queue IPC message
            });

            keyboardInfo->subscribed = true;
        }
    }

    void CKeyboardRegistry::unsubscribeListeners(SKeyboardInfo& keyboardInfo) noexcept {
        keyboardInfo.keyEventListener.reset();
        keyboardInfo.modifiersListener.reset();
        keyboardInfo.keymapListener.reset();
        keyboardInfo.repeatInfoListener.reset();

        keyboardInfo.subscribed = false;
    }

    void CKeyboardRegistry::scheduleRemoval(SKeyboardInfo& keyboardInfo) noexcept {
        keyboardInfo.pendingRemoval = true;
        unsubscribeListeners(keyboardInfo);

        if (m_eventSource) {
            return;
        }

        m_eventSource.reset(::wl_event_loop_add_idle(
            g_pCompositor->m_wlEventLoop,
            [](void* data) {
                auto* self = static_cast<CKeyboardRegistry*>(data);

                [[maybe_unused]]
                auto* obj = self->m_eventSource.release();

                std::erase_if(self->m_keyboardRegistry, [](const auto& record) { return record->pendingRemoval; });
            },
            this));
    }
}
