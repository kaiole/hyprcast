#pragma once

#include "KeyboardId.hpp"

#include <string_view>

// Names used on the event socket. The plugin writes one JSON object per line; the
// overlay writes newline-terminated commands. Both sides use these constants so the
// wire format is defined in one place.
namespace Hyprcast::Wire {
    // Events (plugin -> overlay).
    inline constexpr std::string_view EVENT_CASTING_STATE        = "casting_state";
    inline constexpr std::string_view EVENT_SUBSCRIBE_KEYBOARD   = "subscribe_keyboard";
    inline constexpr std::string_view EVENT_UNSUBSCRIBE_KEYBOARD = "unsubscribe_keyboard";
    inline constexpr std::string_view EVENT_KEY                  = "key";
    inline constexpr std::string_view EVENT_MODIFIERS            = "modifiers";
    inline constexpr std::string_view EVENT_KEYMAP               = "keymap";
    inline constexpr std::string_view EVENT_REPEAT_INFO          = "repeat_info";

    // Fields.
    inline constexpr std::string_view FIELD_EVENT       = "event";
    inline constexpr std::string_view FIELD_KEYBOARD_ID = "keyboard_id";
    inline constexpr std::string_view FIELD_PAUSED      = "paused";
    inline constexpr std::string_view FIELD_NAME        = "name";
    inline constexpr std::string_view FIELD_KEYMAP      = "keymap";
    inline constexpr std::string_view FIELD_TIME_MS     = "time_ms";
    inline constexpr std::string_view FIELD_KEYCODE     = "keycode";
    inline constexpr std::string_view FIELD_STATE       = "state";
    inline constexpr std::string_view FIELD_DEPRESSED   = "depressed";
    inline constexpr std::string_view FIELD_LATCHED     = "latched";
    inline constexpr std::string_view FIELD_LOCKED      = "locked";
    inline constexpr std::string_view FIELD_GROUP       = "group";
    inline constexpr std::string_view FIELD_RATE        = "rate";
    inline constexpr std::string_view FIELD_DELAY       = "delay";

    // Values of FIELD_STATE.
    inline constexpr std::string_view STATE_PRESSED  = "pressed";
    inline constexpr std::string_view STATE_RELEASED = "released";

    // Events that are not about one keyboard carry this id; real keyboard ids start at 1.
    inline constexpr KeyboardId GLOBAL_KEYBOARD_ID = 0;

    // Commands (overlay -> plugin).
    inline constexpr std::string_view COMMAND_ENABLE  = "enable";
    inline constexpr std::string_view COMMAND_DISABLE = "disable";
}
