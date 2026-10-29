#pragma once

#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <expected>
#include <variant>

namespace Hyprcast::Overlay {
    struct CastingStateMessage {
        bool paused = false;
    };

    struct KeyboardSnapshotMessage {
        KeyboardId  id = 0;
        QString     name;
        SModifiers  modifiers;
        QString     keymap;
        SRepeatInfo repeatInfo;
    };

    struct UnsubscribeKeyboardMessage {
        KeyboardId id = 0;
    };

    struct KeyMessage {
        KeyboardId    keyboardId = 0;
        std::uint32_t timeMs     = 0;
        std::uint32_t keycode    = 0;
        bool          pressed    = false;
    };

    struct ModifiersMessage {
        KeyboardId keyboardId = 0;
        SModifiers modifiers;
    };

    struct KeymapMessage {
        KeyboardId keyboardId = 0;
        QString    keymap;
    };

    struct RepeatInfoMessage {
        KeyboardId  keyboardId = 0;
        SRepeatInfo repeatInfo;
    };

    using ProtocolMessage = std::variant<CastingStateMessage, KeyboardSnapshotMessage, UnsubscribeKeyboardMessage, KeyMessage, ModifiersMessage, KeymapMessage, RepeatInfoMessage>;

    // Parses one JSON line from the plugin, or explains why it is invalid.
    [[nodiscard]] std::expected<ProtocolMessage, QString> parseMessage(const QByteArray& jsonLine);
} // namespace Hyprcast::Overlay
