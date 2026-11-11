#pragma once

#include "hyprcast/protocol/key_event.hpp"
#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/keyboard_snapshot.hpp"
#include "hyprcast/protocol/keymap_text.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/repeat_info.hpp"

#include <string>

// Encodes each event the plugin sends as one JSON object. The result has no
// trailing newline; the event server adds the line terminator. Field and event
// names come from hyprcast/protocol/wire.hpp.
namespace hyprcast::wire {

// The first line of every connection: the protocol version the plugin speaks.
[[nodiscard]] std::string hello_message();
[[nodiscard]] std::string casting_state_message(bool is_paused);
[[nodiscard]] std::string subscribe_message(keyboard_snapshot snapshot);
[[nodiscard]] std::string unsubscribe_message(keyboard_id id);
[[nodiscard]] std::string key_message(keyboard_id id, const key_event& event);
[[nodiscard]] std::string modifiers_message(keyboard_id id,
                                            const modifier_state& modifiers);
[[nodiscard]] std::string keymap_message(keyboard_id id, keymap_text keymap);
[[nodiscard]] std::string repeat_info_message(keyboard_id id,
                                              const repeat_info& info);

} // namespace hyprcast::wire
