#pragma once

#include "Protocol.hpp"

#include <QSet>
#include <QString>
#include <expected>
#include <optional>

namespace hyprcast::overlay {

// Connection-scoped view of the plugin's state, validated message by message.
// It tracks only what validation needs; the keyboards themselves live in the
// interpreter.
class ConnectionState {
public:
    [[nodiscard]] std::expected<void, QString>
    apply(const ProtocolMessage& message);
    void reset() noexcept;

    [[nodiscard]] bool hasCastingState() const noexcept {
        return m_paused.has_value();
    }
    [[nodiscard]] bool paused() const noexcept {
        return m_paused.value_or(false);
    }
    [[nodiscard]] int keyboardCount() const noexcept {
        return static_cast<int>(m_keyboards.size());
    }

private:
    std::optional<bool> m_paused;
    QSet<KeyboardId>
        m_keyboards; // Subscribed keyboards; never contains the reserved id 0.
};

} // namespace hyprcast::overlay
