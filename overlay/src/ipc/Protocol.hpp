#pragma once

#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <expected>
#include <variant>

namespace hyprcast::overlay {

struct CastingStateMessage {
    bool paused = false;
};

struct KeyboardSnapshotMessage {
    keyboard_id id = 0;
    QString name {};
    modifier_state modifiers {};
    QString keymap {};
    repeat_info repeatInfo {};
};

struct UnsubscribeKeyboardMessage {
    keyboard_id id = 0;
};

struct KeyMessage {
    keyboard_id keyboardId = 0;
    std::uint32_t timeMs = 0;
    std::uint32_t keycode = 0;
    bool pressed = false;
};

struct ModifiersMessage {
    keyboard_id keyboardId = 0;
    modifier_state modifiers {};
};

struct KeymapMessage {
    keyboard_id keyboardId = 0;
    QString keymap {};
};

struct RepeatInfoMessage {
    keyboard_id keyboardId = 0;
    repeat_info repeatInfo {};
};

using ProtocolMessage = std::variant<CastingStateMessage,
                                     KeyboardSnapshotMessage,
                                     UnsubscribeKeyboardMessage,
                                     KeyMessage,
                                     ModifiersMessage,
                                     KeymapMessage,
                                     RepeatInfoMessage>;

// Parses one JSON line from the plugin, or explains why it is invalid.
[[nodiscard]] std::expected<ProtocolMessage, QString>
parseMessage(const QByteArray& jsonLine);

} // namespace hyprcast::overlay
