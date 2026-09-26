#include "ConnectionState.hpp"

#include <type_traits>

namespace Hyprcast::Overlay {
    bool ConnectionState::apply(const ProtocolMessage& message, QString* error) {
        if (error) {
            error->clear();
        }
        return std::visit(
            [this, error](const auto& event) {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<T, CastingStateMessage>) {
                    m_paused           = event.paused;
                    m_lastEventSummary = event.paused ? QStringLiteral("Casting paused") : QStringLiteral("Casting resumed");
                    return true;
                } else if constexpr (std::is_same_v<T, KeyboardSnapshotMessage>) {
                    if (event.id == 0) {
                        if (error) {
                            *error = QStringLiteral("keyboard snapshot uses reserved keyboard_id 0");
                        }
                        return false;
                    }
                    const auto    id   = event.id;
                    const QString name = event.name;
                    m_keyboards.insert(id,
                                       KeyboardState{.id          = id,
                                                     .name        = event.name,
                                                     .depressed   = event.depressed,
                                                     .latched     = event.latched,
                                                     .locked      = event.locked,
                                                     .group       = event.group,
                                                     .keymap      = event.keymap,
                                                     .repeatRate  = event.repeatRate,
                                                     .repeatDelay = event.repeatDelay});
                    m_lastEventSummary = QStringLiteral("Keyboard subscribed: %1 (#%2)").arg(name).arg(id);
                    return true;
                } else if constexpr (std::is_same_v<T, UnsubscribeKeyboardMessage>) {
                    if (event.id == 0) {
                        if (error) {
                            *error = QStringLiteral("unsubscribe uses reserved keyboard_id 0");
                        }
                        return false;
                    }
                    m_keyboards.remove(event.id);
                    m_lastEventSummary = QStringLiteral("Keyboard unsubscribed: #%1").arg(event.id);
                    return true;
                } else if constexpr (std::is_same_v<T, KeyMessage>) {
                    if (!requireKeyboard(event.keyboardId, error)) {
                        return false;
                    }
                    m_lastEventSummary = QStringLiteral("Key %1 %2 · keyboard #%3")
                                             .arg(event.keycode)
                                             .arg(event.pressed ? QStringLiteral("pressed") : QStringLiteral("released"))
                                             .arg(event.keyboardId);
                    return true;
                } else if constexpr (std::is_same_v<T, ModifiersMessage>) {
                    if (!requireKeyboard(event.keyboardId, error)) {
                        return false;
                    }
                    auto& keyboard     = m_keyboards[event.keyboardId];
                    keyboard.depressed = event.depressed;
                    keyboard.latched   = event.latched;
                    keyboard.locked    = event.locked;
                    keyboard.group     = event.group;
                    m_lastEventSummary = QStringLiteral("Modifiers updated · keyboard #%1").arg(event.keyboardId);
                    return true;
                } else if constexpr (std::is_same_v<T, KeymapMessage>) {
                    if (!requireKeyboard(event.keyboardId, error)) {
                        return false;
                    }
                    m_keyboards[event.keyboardId].keymap = event.keymap;
                    m_lastEventSummary                   = QStringLiteral("Keymap updated · keyboard #%1").arg(event.keyboardId);
                    return true;
                } else if constexpr (std::is_same_v<T, RepeatInfoMessage>) {
                    if (!requireKeyboard(event.keyboardId, error)) {
                        return false;
                    }
                    auto& keyboard       = m_keyboards[event.keyboardId];
                    keyboard.repeatRate  = event.rate;
                    keyboard.repeatDelay = event.delay;
                    m_lastEventSummary   = QStringLiteral("Repeat settings updated · keyboard #%1").arg(event.keyboardId);
                    return true;
                }
            },
            message);
    }

    void ConnectionState::reset() noexcept {
        m_paused.reset();
        m_keyboards.clear();
        m_lastEventSummary.clear();
    }

    bool ConnectionState::requireKeyboard(std::uint32_t id, QString* error) const {
        if (id != 0 && m_keyboards.contains(id)) {
            return true;
        }
        if (error) {
            *error = QStringLiteral("event references unsubscribed keyboard_id %1").arg(id);
        }
        return false;
    }
} // namespace Hyprcast::Overlay
