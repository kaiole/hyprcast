#pragma once

#include "Protocol.hpp"

#include <QMap>
#include <QString>

#include <expected>
#include <optional>

namespace Hyprcast::Overlay {
    // Connection-scoped view of the plugin's state, validated message by message.
    class ConnectionState {
      public:
        [[nodiscard]] std::expected<void, QString> apply(const ProtocolMessage& message);
        void                                       reset() noexcept;

        [[nodiscard]] bool                         hasCastingState() const noexcept {
            return m_paused.has_value();
        }
        [[nodiscard]] bool paused() const noexcept {
            return m_paused.value_or(false);
        }
        [[nodiscard]] const QMap<KeyboardId, KeyboardSnapshotMessage>& keyboards() const noexcept {
            return m_keyboards;
        }
        [[nodiscard]] const QString& lastEventSummary() const noexcept {
            return m_lastEventSummary;
        }

      private:
        // One overload per message type; std::visit fails to compile if one is missing.
        std::expected<void, QString>              applyEvent(const CastingStateMessage& event);
        std::expected<void, QString>              applyEvent(const KeyboardSnapshotMessage& event);
        std::expected<void, QString>              applyEvent(const UnsubscribeKeyboardMessage& event);
        std::expected<void, QString>              applyEvent(const KeyMessage& event);
        std::expected<void, QString>              applyEvent(const ModifiersMessage& event);
        std::expected<void, QString>              applyEvent(const KeymapMessage& event);
        std::expected<void, QString>              applyEvent(const RepeatInfoMessage& event);

        [[nodiscard]] KeyboardSnapshotMessage*    subscribed(KeyboardId id);

        std::optional<bool>                       m_paused;
        QMap<KeyboardId, KeyboardSnapshotMessage> m_keyboards;
        QString                                   m_lastEventSummary;
    };
} // namespace Hyprcast::Overlay
