#include "ConnectionState.hpp"

namespace Hyprcast::Overlay {
    namespace {
        std::unexpected<QString> unsubscribedKeyboard(KeyboardId id) {
            return std::unexpected(QStringLiteral("event references unsubscribed keyboard_id %1").arg(id));
        }
    }

    std::expected<void, QString> ConnectionState::apply(const ProtocolMessage& message) {
        return std::visit([this](const auto& event) { return applyEvent(event); }, message);
    }

    void ConnectionState::reset() noexcept {
        m_paused.reset();
        m_keyboards.clear();
        m_lastEventSummary.clear();
    }

    std::expected<void, QString> ConnectionState::applyEvent(const CastingStateMessage& event) {
        m_paused           = event.paused;
        m_lastEventSummary = event.paused ? QStringLiteral("Casting paused") : QStringLiteral("Casting resumed");
        return {};
    }

    std::expected<void, QString> ConnectionState::applyEvent(const KeyboardSnapshotMessage& event) {
        if (event.id == 0) {
            return std::unexpected(QStringLiteral("keyboard snapshot uses reserved keyboard_id 0"));
        }
        m_keyboards.insert(event.id, event);
        m_lastEventSummary = QStringLiteral("Keyboard subscribed: %1 (#%2)").arg(event.name).arg(event.id);
        return {};
    }

    std::expected<void, QString> ConnectionState::applyEvent(const UnsubscribeKeyboardMessage& event) {
        if (event.id == 0) {
            return std::unexpected(QStringLiteral("unsubscribe uses reserved keyboard_id 0"));
        }
        m_keyboards.remove(event.id);
        m_lastEventSummary = QStringLiteral("Keyboard unsubscribed: #%1").arg(event.id);
        return {};
    }

    std::expected<void, QString> ConnectionState::applyEvent(const KeyMessage& event) {
        if (!subscribed(event.keyboardId)) {
            return unsubscribedKeyboard(event.keyboardId);
        }
        m_lastEventSummary =
            QStringLiteral("Key %1 %2 · keyboard #%3").arg(event.keycode).arg(event.pressed ? QStringLiteral("pressed") : QStringLiteral("released")).arg(event.keyboardId);
        return {};
    }

    std::expected<void, QString> ConnectionState::applyEvent(const ModifiersMessage& event) {
        auto* keyboard = subscribed(event.keyboardId);
        if (!keyboard) {
            return unsubscribedKeyboard(event.keyboardId);
        }
        keyboard->modifiers = event.modifiers;
        m_lastEventSummary  = QStringLiteral("Modifiers updated · keyboard #%1").arg(event.keyboardId);
        return {};
    }

    std::expected<void, QString> ConnectionState::applyEvent(const KeymapMessage& event) {
        auto* keyboard = subscribed(event.keyboardId);
        if (!keyboard) {
            return unsubscribedKeyboard(event.keyboardId);
        }
        keyboard->keymap   = event.keymap;
        m_lastEventSummary = QStringLiteral("Keymap updated · keyboard #%1").arg(event.keyboardId);
        return {};
    }

    std::expected<void, QString> ConnectionState::applyEvent(const RepeatInfoMessage& event) {
        auto* keyboard = subscribed(event.keyboardId);
        if (!keyboard) {
            return unsubscribedKeyboard(event.keyboardId);
        }
        keyboard->repeatInfo = event.repeatInfo;
        m_lastEventSummary   = QStringLiteral("Repeat settings updated · keyboard #%1").arg(event.keyboardId);
        return {};
    }

    KeyboardSnapshotMessage* ConnectionState::subscribed(KeyboardId id) {
        if (id == 0) {
            return nullptr;
        }
        const auto it = m_keyboards.find(id);
        return it == m_keyboards.end() ? nullptr : &it.value();
    }
} // namespace Hyprcast::Overlay
