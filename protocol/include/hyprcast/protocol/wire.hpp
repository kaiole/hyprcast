#pragma once

#include "keyboard_id.hpp"

#include <cstdint>
#include <string_view>

// Names used on the event socket. The plugin writes one JSON object per line;
// the overlay writes newline-terminated commands. Both sides use these
// constants so the wire format is defined in one place.
namespace hyprcast::wire {

// Raise this with every change to the lines below that an older plugin or
// overlay would misread. The plugin opens each connection with a hello line
// that carries it, and the overlay refuses any other version. The hello line
// itself has to stay as it is, or the two can no longer tell.
inline constexpr std::uint32_t protocol_version = 1;

// Events (plugin -> overlay).
inline constexpr std::string_view event_hello = "hello";
inline constexpr std::string_view event_casting_state = "casting_state";
inline constexpr std::string_view event_subscribe_keyboard =
    "subscribe_keyboard";
inline constexpr std::string_view event_unsubscribe_keyboard =
    "unsubscribe_keyboard";
inline constexpr std::string_view event_key = "key";
inline constexpr std::string_view event_modifiers = "modifiers";
inline constexpr std::string_view event_keymap = "keymap";
inline constexpr std::string_view event_repeat_info = "repeat_info";

// Fields.
inline constexpr std::string_view field_event = "event";
inline constexpr std::string_view field_protocol_version = "protocol_version";
inline constexpr std::string_view field_keyboard_id = "keyboard_id";
inline constexpr std::string_view field_paused = "paused";
inline constexpr std::string_view field_name = "name";
inline constexpr std::string_view field_keymap = "keymap";
inline constexpr std::string_view field_time_ms = "time_ms";
inline constexpr std::string_view field_keycode = "keycode";
inline constexpr std::string_view field_state = "state";
inline constexpr std::string_view field_depressed = "depressed";
inline constexpr std::string_view field_latched = "latched";
inline constexpr std::string_view field_locked = "locked";
inline constexpr std::string_view field_group = "group";
inline constexpr std::string_view field_rate = "rate";
inline constexpr std::string_view field_delay = "delay";

// Values of field_state.
inline constexpr std::string_view state_pressed = "pressed";
inline constexpr std::string_view state_released = "released";

// Events that are not about one keyboard carry this id; real keyboard ids start
// at 1. The hello line has no keyboard id at all.
inline constexpr keyboard_id global_keyboard_id = 0;

// Commands (overlay -> plugin).
inline constexpr std::string_view command_enable = "enable";
inline constexpr std::string_view command_disable = "disable";

} // namespace hyprcast::wire
