#pragma once

#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/repeat_info.hpp"

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

// Checks the line that must open every connection: the plugin's hello with the
// protocol version this overlay speaks. The error tells the user what to
// restart, so it can be shown as it is.
[[nodiscard]] std::expected<void, QString>
checkHello(const QByteArray& jsonLine);

// Parses one JSON line that follows the hello, or explains why it is invalid.
[[nodiscard]] std::expected<ProtocolMessage, QString>
parseMessage(const QByteArray& jsonLine);

} // namespace hyprcast::overlay
