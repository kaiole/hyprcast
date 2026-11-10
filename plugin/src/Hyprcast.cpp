#include "Hyprcast.hpp"

#include "CallbackBoundary.hpp"
#include "Log.hpp"
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

#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace hyprcast {
namespace {

using namespace wire;

[[nodiscard]] std::string key(std::string_view name) {
    return std::string {name};
}

[[nodiscard]] libjson::json_value::object
to_json_object(const key_event& event) {
    return libjson::json_value::object {
        {
            key(field_time_ms),
            {.data = static_cast<std::uint64_t>(event.time_ms)},
        },
        {
            key(field_keycode),
            {.data = static_cast<std::uint64_t>(event.keycode)},
        },
        {
            key(field_state),
            {
                .data = key(event.state == key_state::pressed ? state_pressed
                                                              : state_released),
            },
        },
    };
}

[[nodiscard]] libjson::json_value::object
to_json_object(const modifier_state& modifiers) {
    return libjson::json_value::object {
        {
            key(field_depressed),
            {.data = static_cast<std::uint64_t>(modifiers.depressed)},
        },
        {
            key(field_latched),
            {.data = static_cast<std::uint64_t>(modifiers.latched)},
        },
        {
            key(field_locked),
            {.data = static_cast<std::uint64_t>(modifiers.locked)},
        },
        {
            key(field_group),
            {.data = static_cast<std::uint64_t>(modifiers.group)},
        },
    };
}

[[nodiscard]] libjson::json_value::object
to_json_object(const repeat_info& info) {
    return libjson::json_value::object {
        {
            key(field_rate),
            {.data = static_cast<std::uint64_t>(info.rate)},
        },
        {
            key(field_delay),
            {.data = static_cast<std::uint64_t>(info.delay)},
        },
    };
}

[[nodiscard]] libjson::json_value::object
to_json_object(keyboard_snapshot snapshot) {
    auto json_object = to_json_object(snapshot.modifiers);
    json_object.merge(to_json_object(snapshot.repeat_info));

    json_object.emplace(key(field_name),
                        libjson::json_value {.data = std::move(snapshot.name)});
    json_object.emplace(
        key(field_keymap),
        libjson::json_value {.data = std::move(snapshot.keymap)});

    return json_object;
}

[[nodiscard]] libjson::json_value
make_event(std::string_view event_name,
           keyboard_id id,
           libjson::json_value::object json_object) {
    json_object.emplace(key(field_event),
                        libjson::json_value {.data = key(event_name)});
    json_object.emplace(
        key(field_keyboard_id),
        libjson::json_value {.data = static_cast<std::uint64_t>(id)});

    return {.data = std::move(json_object)};
}

[[nodiscard]] std::string key_message(keyboard_id id, const key_event& event) {
    return libjson::serialize(make_event(event_key, id, to_json_object(event)));
}

[[nodiscard]] std::string modifiers_message(keyboard_id id,
                                            const modifier_state& modifiers) {
    return libjson::serialize(
        make_event(event_modifiers, id, to_json_object(modifiers)));
}

[[nodiscard]] std::string keymap_message(keyboard_id id, keymap_text keymap) {
    libjson::json_value::object json_object;
    json_object.emplace(key(field_keymap),
                        libjson::json_value {.data = std::move(keymap)});

    return libjson::serialize(
        make_event(event_keymap, id, std::move(json_object)));
}

[[nodiscard]] std::string repeat_info_message(keyboard_id id,
                                              const repeat_info& info) {
    return libjson::serialize(
        make_event(event_repeat_info, id, to_json_object(info)));
}

[[nodiscard]] std::string subscribe_message(keyboard_snapshot snapshot) {
    const auto id = snapshot.id;
    return libjson::serialize(make_event(event_subscribe_keyboard,
                                         id,
                                         to_json_object(std::move(snapshot))));
}

[[nodiscard]] std::string unsubscribe_message(keyboard_id id) {
    return libjson::serialize(make_event(event_unsubscribe_keyboard,
                                         id,
                                         libjson::json_value::object {}));
}

[[nodiscard]] std::string casting_state_message(bool is_paused) {
    return libjson::serialize(
        make_event(event_casting_state,
                   global_keyboard_id,
                   libjson::json_value::object {
                       {key(field_paused), {.data = is_paused}},
                   }));
}

} // namespace

// Capture starts paused and pauses on disconnect, so only an explicit client
// request enables it.
keycaster::keycaster()
    : server_([this] { request_registry_snapshot(); },
              [this](bool enabled) { set_pause(!enabled); },
              [this] { paused_ = true; }),
      keyboard_registry_(make_registry_callbacks()),
      config_(make_plugin_config_callbacks()) {
    keyboard_registry_.update_subscriptions(config_.get_accepted_config());
}

void keycaster::add_keyboard(const SP<IKeyboard>& keyboard) {
    if (shutdown_) {
        return;
    }

    run_guarded(
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
    server_.queue_message(casting_state_message(paused_));
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
    server_.queue_message(casting_state_message(paused_));

    auto snapshots = keyboard_registry_.get_registry_snapshot();

    for (auto& snapshot : snapshots) {
        server_.queue_message(subscribe_message(std::move(snapshot)));
    }
}

keyboard_registry::callbacks keycaster::make_registry_callbacks() {
    return {
        .on_exception = [this] noexcept { server_.disconnect_client(); },
        .on_subscribe =
            [this](keyboard_snapshot snapshot) {
                server_.queue_message(subscribe_message(std::move(snapshot)));
            },
        .on_unsubscribe =
            [this](keyboard_id id) {
                server_.queue_message(unsubscribe_message(id));
            },
        .on_key_event =
            [this](keyboard_id id, key_event event) {
                if (!paused_) {
                    server_.queue_message(key_message(id, event));
                }
            },
        .on_modifiers_event =
            [this](keyboard_id id, modifier_state modifiers) {
                server_.queue_message(modifiers_message(id, modifiers));
            },
        .on_keymap_event =
            [this](keyboard_id id, keymap_text keymap) {
                server_.queue_message(keymap_message(id, std::move(keymap)));
            },
        .on_repeat_info_event =
            [this](keyboard_id id, repeat_info info) {
                server_.queue_message(repeat_info_message(id, info));
            },
    };
}

void keycaster::shutdown() noexcept {
    if (shutdown_) {
        return;
    }
    notify_failure(
        "Plugin stopped after an internal failure. Unload and reload "
        "hyprcast; see Hyprland logs for details.");
    keyboard_registry_.shutdown();
    server_.shutdown();
    shutdown_ = true;
}

} // namespace hyprcast
