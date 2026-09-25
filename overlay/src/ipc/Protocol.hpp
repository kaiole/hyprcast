#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <variant>

namespace Hyprcast::Overlay {
    struct CastingStateMessage {
        bool paused = false;
    };

    struct KeyboardSnapshotMessage {
        std::uint32_t id = 0;
        QString       name;
        std::uint32_t depressed = 0;
        std::uint32_t latched   = 0;
        std::uint32_t locked    = 0;
        std::uint32_t group     = 0;
        QString       keymap;
        std::uint32_t repeatRate  = 0;
        std::uint32_t repeatDelay = 0;
    };

    struct UnsubscribeKeyboardMessage {
        std::uint32_t id = 0;
    };

    struct KeyMessage {
        std::uint32_t keyboardId = 0;
        std::uint32_t timeMs     = 0;
        std::uint32_t keycode    = 0;
        bool          pressed    = false;
    };

    struct ModifiersMessage {
        std::uint32_t keyboardId = 0;
        std::uint32_t depressed  = 0;
        std::uint32_t latched    = 0;
        std::uint32_t locked     = 0;
        std::uint32_t group      = 0;
    };

    struct KeymapMessage {
        std::uint32_t keyboardId = 0;
        QString       keymap;
    };

    struct RepeatInfoMessage {
        std::uint32_t keyboardId = 0;
        std::uint32_t rate       = 0;
        std::uint32_t delay      = 0;
    };

    using ProtocolMessage = std::variant<CastingStateMessage, KeyboardSnapshotMessage, UnsubscribeKeyboardMessage, KeyMessage, ModifiersMessage, KeymapMessage, RepeatInfoMessage>;

    [[nodiscard]] bool parseMessage(const QByteArray& jsonLine, ProtocolMessage* message, QString* error);
} // namespace Hyprcast::Overlay
