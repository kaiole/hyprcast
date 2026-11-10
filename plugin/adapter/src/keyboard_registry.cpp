#include "keyboard_registry.hpp"

#include "hyprcast/config/config.hpp"
#include "hyprcast/log/log_sink.hpp"
#include "hyprcast/protocol/key_event.hpp"
#include "hyprcast/protocol/key_state.hpp"
#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/keyboard_snapshot.hpp"
#include "hyprcast/protocol/keymap_text.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/registry_snapshot.hpp"
#include "hyprcast/protocol/repeat_info.hpp"
#include "hyprcast/session/session.hpp"
#include "keyboard_info.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace hyprcast {
namespace {

key_event to_hyprcast_type(const IKeyboard::SKeyEvent& event) noexcept {
    const auto state = event.state == WL_KEYBOARD_KEY_STATE_PRESSED
        ? key_state::pressed
        : key_state::released;

    return {.time_ms = event.timeMs, .keycode = event.keycode, .state = state};
}

modifier_state
to_hyprcast_type(const IKeyboard::SModifiersEvent& event) noexcept {
    return {
        .depressed = event.depressed,
        .latched = event.latched,
        .locked = event.locked,
        .group = event.group,
    };
}

// Stateless, so the unique_ptr stays pointer-sized (a function-pointer deleter
// would not).
struct free_deleter {
    void operator()(char* string) const noexcept {
        // libxkbcommon allocates this with malloc.
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
        std::free(string);
    }
};

keymap_text to_hyprcast_type(const IKeyboard::SKeymapEvent& event) {
    const std::unique_ptr<char, free_deleter> keymap_string {
        ::xkb_keymap_get_as_string(event.keymap, XKB_KEYMAP_FORMAT_TEXT_V1)
    };
    if (!keymap_string) {
        throw std::runtime_error("Cannot serialize XKB keymap");
    }

    return keymap_string.get();
}

// Hyprland stores these as int; clamp so a bogus negative value cannot wrap.
repeat_info to_hyprcast_type(int repeat_rate, int repeat_delay) noexcept {
    return {
        .rate = static_cast<std::uint32_t>(std::max(repeat_rate, 0)),
        .delay = static_cast<std::uint32_t>(std::max(repeat_delay, 0)),
    };
}

keyboard_snapshot get_keyboard_snapshot(const IKeyboard& keyboard,
                                        keyboard_id id,
                                        const std::string& name) {
    return {
        .id = id,
        .name = name,
        .modifiers =
            {
                .depressed = keyboard.m_modifiersState.depressed,
                .latched = keyboard.m_modifiersState.latched,
                .locked = keyboard.m_modifiersState.locked,
                .group = keyboard.m_modifiersState.group,
            },
        // Keep subscription snapshots consistent with incremental keymap events
        // and interoperable with Wayland clients: both use libxkbcommon text
        // V1.
        .keymap = keyboard.m_xkbKeymapV1String,
        .repeat_info =
            to_hyprcast_type(keyboard.m_repeatRate, keyboard.m_repeatDelay),
    };
}

} // namespace

keyboard_registry::keyboard_registry(log_sink& log, session& events)
    : log_(&log),
      session_(&events) {
    for (auto& keyboard : g_pInputManager->m_keyboards) {
        add_keyboard(keyboard);
    }

    // In Lua config reloads, config.reloaded fires before the deferred keyboard
    // refresh. This notification fires after applyConfigToKeyboard sets the
    // keymap.
    layout_listener_ = Event::bus()->m_events.input.keyboard.layout.listen(
        [this](SP<IKeyboard> keyboard, const std::string&) {
            run_guarded(
                "Cannot process keyboard layout update; disconnecting client",
                [this, &keyboard] { sync_keymap(keyboard); });
        });
}

void keyboard_registry::add_keyboard(const SP<IKeyboard>& keyboard) {
    auto info = std::make_unique<keyboard_info>();

    info->keyboard = keyboard;
    info->id = next_id_++;
    info->name = keyboard->m_hlName;

    info->destroy_listener = keyboard->m_events.destroy.listen(
        [this, record = info.get()] { schedule_removal(*record); });

    keyboard_registry_.push_back(std::move(info));
}

void keyboard_registry::update_subscriptions(const config& accepted_config) {
    for (const auto& info : keyboard_registry_) {
        if (info->pending_removal) {
            continue;
        }

        const auto it =
            std::ranges::find(accepted_config.filtered_keyboards, info->name);
        if ((accepted_config.filter_setting == keyboard_filter_setting::include
             && it == accepted_config.filtered_keyboards.end())
            || (accepted_config.filter_setting
                    == keyboard_filter_setting::exclude
                && it != accepted_config.filtered_keyboards.end())) {
            unsubscribe_listeners(*info);
            continue;
        }

        if (info->subscribed) {
            continue;
        }

        const auto keyboard = info->keyboard.lock();
        if (!keyboard) {
            continue;
        }

        keyboard_id id = info->id;

        info->key_event_listener = keyboard->m_keyboardEvents.key.listen(
            [this, id](const IKeyboard::SKeyEvent& event) {
                run_guarded("Cannot process key event; disconnecting client",
                            [this, id, &event] {
                                session_->on_key_event(id,
                                                       to_hyprcast_type(event));
                            });
            });

        info->modifiers_listener = keyboard->m_keyboardEvents.modifiers.listen(
            [this, id](const IKeyboard::SModifiersEvent& event) {
                run_guarded(
                    "Cannot process modifiers update; disconnecting client",
                    [this, id, &event] {
                        session_->on_modifiers_event(id,
                                                     to_hyprcast_type(event));
                    });
            });

        info->keymap_listener = keyboard->m_keyboardEvents.keymap.listen(
            [this, id, info = info.get()](
                const IKeyboard::SKeymapEvent& event) {
                run_guarded(
                    "Cannot process keymap update; disconnecting client",
                    [this, id, info, &event] {
                        auto keymap = to_hyprcast_type(event);
                        info->last_sent_keymap = keymap;
                        session_->on_keymap_event(id, std::move(keymap));
                    });
            });

        info->repeat_info_listener =
            keyboard->m_keyboardEvents.repeatInfo.listen(
                [this, id, weak_keyboard = info->keyboard] {
                    auto locked_keyboard = weak_keyboard.lock();
                    if (!locked_keyboard) {
                        return;
                    }

                    run_guarded(
                        "Cannot process repeat settings update; disconnecting "
                        "client",
                        [this, id, &locked_keyboard] {
                            session_->on_repeat_info_event(
                                id,
                                to_hyprcast_type(
                                    locked_keyboard->m_repeatRate,
                                    locked_keyboard->m_repeatDelay));
                        });
                });

        info->subscribed = true;
        log_message(*log_,
                    log_level::trace,
                    "Subscribed to keyboard {} ('{}')",
                    id,
                    info->name);
        run_guarded(
            "Cannot prepare keyboard subscription; disconnecting client",
            [this, &keyboard, id, &info] {
                auto snapshot =
                    get_keyboard_snapshot(*keyboard, id, info->name);
                info->last_sent_keymap = snapshot.keymap;

                session_->on_subscribe(std::move(snapshot));
            });
    }
}

registry_snapshot keyboard_registry::get_registry_snapshot() {
    registry_snapshot snapshots {};

    for (const auto& info : keyboard_registry_) {
        if (!info->subscribed) {
            continue;
        }

        const auto keyboard = info->keyboard.lock();
        if (!keyboard) {
            continue;
        }

        auto snapshot = get_keyboard_snapshot(*keyboard, info->id, info->name);
        info->last_sent_keymap = snapshot.keymap;
        snapshots.push_back(std::move(snapshot));
    }

    return snapshots;
}

void keyboard_registry::shutdown() noexcept {
    layout_listener_.reset();
    wl_idle_keyboard_removal_.reset();

    for (auto& info : keyboard_registry_) {
        info->key_event_listener.reset();
        info->modifiers_listener.reset();
        info->keymap_listener.reset();
        info->repeat_info_listener.reset();
        info->destroy_listener.reset();

        info->subscribed = false;
    }
}

void keyboard_registry::sync_keymap(const SP<IKeyboard>& keyboard) {
    if (!keyboard) {
        return;
    }

    const auto it =
        std::ranges::find_if(keyboard_registry_, [&keyboard](const auto& info) {
            return info->subscribed && info->keyboard.lock() == keyboard;
        });
    if (it == keyboard_registry_.end()) {
        return;
    }

    auto& info = **it;
    const auto& active_keymap = keyboard->m_xkbKeymapV1String;
    if (info.last_sent_keymap == active_keymap) {
        return;
    }

    session_->on_keymap_event(info.id, active_keymap);
    info.last_sent_keymap = active_keymap;
}

void keyboard_registry::schedule_removal(keyboard_info& info) noexcept {
    info.pending_removal = true;
    unsubscribe_listeners(info);

    if (wl_idle_keyboard_removal_) {
        return;
    }

    // If scheduling fails, retain the inactive records and retry on the next
    // schedule_removal() call.
    wl_idle_keyboard_removal_.reset(::wl_event_loop_add_idle(
        g_pCompositor->m_wlEventLoop,
        [](void* data) {
            auto* self = static_cast<keyboard_registry*>(data);

            // Wayland removes this source after the callback returns; release
            // ownership to avoid double removal.
            [[maybe_unused]] auto* obj =
                self->wl_idle_keyboard_removal_.release();

            std::erase_if(self->keyboard_registry_, [](const auto& record) {
                return record->pending_removal;
            });
        },
        this));
    if (!wl_idle_keyboard_removal_) {
        log_message(*log_,
                    log_level::warning,
                    "Cannot schedule keyboard cleanup; retaining inactive "
                    "records until next removal");
    }
}

void keyboard_registry::unsubscribe_listeners(keyboard_info& info) noexcept {
    if (!info.subscribed) {
        return;
    }

    info.key_event_listener.reset();
    info.modifiers_listener.reset();
    info.keymap_listener.reset();
    info.repeat_info_listener.reset();

    info.subscribed = false;
    log_message(*log_,
                log_level::trace,
                "Unsubscribed from keyboard {} ('{}')",
                info.id,
                info.name);
    run_guarded("Cannot prepare keyboard unsubscription; disconnecting client",
                [this, &info] { session_->on_unsubscribe(info.id); });
}

} // namespace hyprcast
