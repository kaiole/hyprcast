#pragma once

#include "Protocol.hpp"

#include <QMap>
#include <QString>

#include <optional>
#include <utility>

namespace Hyprcast::Overlay {
    struct KeyboardState {
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

    class ConnectionState {
      public:
        [[nodiscard]] bool apply(ProtocolMessage message, QString* error);
        void               reset() noexcept;

        [[nodiscard]] bool hasCastingState() const noexcept {
            return m_paused.has_value();
        }
        [[nodiscard]] bool paused() const noexcept {
            return m_paused.value_or(false);
        }
        [[nodiscard]] const QMap<std::uint32_t, KeyboardState>& keyboards() const noexcept {
            return m_keyboards;
        }
        [[nodiscard]] const QString& lastEventSummary() const noexcept {
            return m_lastEventSummary;
        }

      private:
        [[nodiscard]] bool                 requireKeyboard(std::uint32_t id, QString* error) const;

        std::optional<bool>                m_paused;
        QMap<std::uint32_t, KeyboardState> m_keyboards;
        QString                            m_lastEventSummary;
    };
} // namespace Hyprcast::Overlay
