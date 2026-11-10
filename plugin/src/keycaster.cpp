#include "keycaster.hpp"

#include "hyprcast/log/run_guarded.hpp"
#include "hyprcast/protocol/key_event.hpp"
#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/keyboard_snapshot.hpp"
#include "hyprcast/protocol/keymap_text.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/repeat_info.hpp"
#include "hyprcast/server/event_server.hpp"
#include "hyprcast/wire/messages.hpp"
#include "hyprland/keyboard_registry.hpp"
#include "hyprland/plugin_config.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>

#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace hyprcast {

// Capture starts paused and pauses on disconnect, so only an explicit client
// request enables it.
keycaster::keycaster()
    : server_(
          g_pCompositor->m_wlEventLoop,
          event_socket_dir(std::getenv("XDG_RUNTIME_DIR"),
                           g_pCompositor->m_instanceSignature),
          log_,
          [this] { request_registry_snapshot(); },
          [this](bool enabled) { set_pause(!enabled); },
          [this] { paused_ = true; }),
      keyboard_registry_(log_, make_registry_callbacks()),
      config_(log_, make_plugin_config_callbacks()) {
    keyboard_registry_.update_subscriptions(config_.get_accepted_config());
}

void keycaster::add_keyboard(const SP<IKeyboard>& keyboard) {
    if (shutdown_) {
        return;
    }

    run_guarded(
        log_,
        "Cannot register keyboard; shutting down plugin",
        [this, &keyboard] {
            keyboard_registry_.add_keyboard(keyboard);
            keyboard_registry_.update_subscriptions(
                config_.get_accepted_config());
        },
        [this] noexcept { shutdown(); });
}

void keycaster::set_pause(bool paused) {
    if (shutdown_) {
        throw std::runtime_error("Hyprcast is stopped; reload the plugin");
    }

    // Always echo the authoritative state, including idempotent control
    // requests.
    paused_ = paused;
    server_.queue_message(wire::casting_state_message(paused_));
}

bool keycaster::is_paused() const noexcept {
    return paused_;
}

plugin_config::callbacks keycaster::make_plugin_config_callbacks() {
    return {
        .on_config_reload =
            [this] {
                if (shutdown_) {
                    return;
                }

                keyboard_registry_.update_subscriptions(
                    config_.get_accepted_config());
            },
        .on_exception = [this] noexcept { shutdown(); },
    };
}

void keycaster::request_registry_snapshot() {
    server_.queue_message(wire::casting_state_message(paused_));

    auto snapshots = keyboard_registry_.get_registry_snapshot();

    for (auto& snapshot : snapshots) {
        server_.queue_message(wire::subscribe_message(std::move(snapshot)));
    }
}

keyboard_registry::callbacks keycaster::make_registry_callbacks() {
    return {
        .on_exception = [this] noexcept { server_.disconnect_client(); },
        .on_subscribe =
            [this](keyboard_snapshot snapshot) {
                server_.queue_message(
                    wire::subscribe_message(std::move(snapshot)));
            },
        .on_unsubscribe =
            [this](keyboard_id id) {
                server_.queue_message(wire::unsubscribe_message(id));
            },
        .on_key_event =
            [this](keyboard_id id, key_event event) {
                if (!paused_) {
                    server_.queue_message(wire::key_message(id, event));
                }
            },
        .on_modifiers_event =
            [this](keyboard_id id, modifier_state modifiers) {
                server_.queue_message(wire::modifiers_message(id, modifiers));
            },
        .on_keymap_event =
            [this](keyboard_id id, keymap_text keymap) {
                server_.queue_message(
                    wire::keymap_message(id, std::move(keymap)));
            },
        .on_repeat_info_event =
            [this](keyboard_id id, repeat_info info) {
                server_.queue_message(wire::repeat_info_message(id, info));
            },
    };
}

void keycaster::shutdown() noexcept {
    if (shutdown_) {
        return;
    }
    log_.notify_failure(
        "Plugin stopped after an internal failure. Unload and reload "
        "hyprcast; see Hyprland logs for details.");
    keyboard_registry_.shutdown();
    server_.shutdown();
    shutdown_ = true;
}

} // namespace hyprcast
