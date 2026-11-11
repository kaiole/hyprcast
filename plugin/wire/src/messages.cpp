#include "hyprcast/wire/messages.hpp"

#include "hyprcast/protocol/key_event.hpp"
#include "hyprcast/protocol/key_state.hpp"
#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/keyboard_snapshot.hpp"
#include "hyprcast/protocol/keymap_text.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/repeat_info.hpp"
#include "hyprcast/protocol/wire.hpp"
#include "libjson/json_value.hpp"
#include "libjson/serializer.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace hyprcast::wire {
namespace {

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

} // namespace

std::string key_message(keyboard_id id, const key_event& event) {
    return libjson::serialize(make_event(event_key, id, to_json_object(event)));
}

std::string modifiers_message(keyboard_id id, const modifier_state& modifiers) {
    return libjson::serialize(
        make_event(event_modifiers, id, to_json_object(modifiers)));
}

std::string keymap_message(keyboard_id id, keymap_text keymap) {
    libjson::json_value::object json_object;
    json_object.emplace(key(field_keymap),
                        libjson::json_value {.data = std::move(keymap)});

    return libjson::serialize(
        make_event(event_keymap, id, std::move(json_object)));
}

std::string repeat_info_message(keyboard_id id, const repeat_info& info) {
    return libjson::serialize(
        make_event(event_repeat_info, id, to_json_object(info)));
}

std::string subscribe_message(keyboard_snapshot snapshot) {
    const auto id = snapshot.id;
    return libjson::serialize(make_event(event_subscribe_keyboard,
                                         id,
                                         to_json_object(std::move(snapshot))));
}

std::string unsubscribe_message(keyboard_id id) {
    return libjson::serialize(make_event(event_unsubscribe_keyboard,
                                         id,
                                         libjson::json_value::object {}));
}

std::string hello_message() {
    libjson::json_value::object json_object;
    json_object.emplace(key(field_event),
                        libjson::json_value {.data = key(event_hello)});
    json_object.emplace(
        key(field_protocol_version),
        libjson::json_value {
            .data = static_cast<std::uint64_t>(protocol_version),
        });

    return libjson::serialize({.data = std::move(json_object)});
}

std::string casting_state_message(bool is_paused) {
    return libjson::serialize(
        make_event(event_casting_state,
                   global_keyboard_id,
                   libjson::json_value::object {
                       {key(field_paused), {.data = is_paused}},
                   }));
}

} // namespace hyprcast::wire
