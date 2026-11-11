#include "ConnectionState.hpp"

namespace Hyprcast::Overlay {
    namespace {
        template <typename... Handlers>
        struct Overloaded : Handlers... {
            using Handlers::operator()...;
        };
    } // namespace

    std::expected<void, QString> ConnectionState::apply(const ProtocolMessage& message) {
        using Result = std::expected<void, QString>;
        return std::visit(Overloaded{
                              [this](const CastingStateMessage& event) -> Result {
                                  m_paused = event.paused;
                                  return {};
                              },
                              [this](const KeyboardSnapshotMessage& event) -> Result {
                                  if (event.id == 0) {
                                      return std::unexpected(QStringLiteral("keyboard snapshot uses reserved keyboard_id 0"));
                                  }
                                  m_keyboards.insert(event.id);
                                  return {};
                              },
                              [this](const UnsubscribeKeyboardMessage& event) -> Result {
                                  if (event.id == 0) {
                                      return std::unexpected(QStringLiteral("unsubscribe uses reserved keyboard_id 0"));
                                  }
                                  m_keyboards.remove(event.id);
                                  return {};
                              },
                              // Every remaining message is an event of one keyboard, which must be subscribed.
                              // A new message type without a keyboardId fails to compile here.
                              [this](const auto& event) -> Result {
                                  if (!m_keyboards.contains(event.keyboardId)) {
                                      return std::unexpected(QStringLiteral("event references unsubscribed keyboard_id %1").arg(event.keyboardId));
                                  }
                                  return {};
                              },
                          },
                          message);
    }

    void ConnectionState::reset() noexcept {
        m_paused.reset();
        m_keyboards.clear();
    }
} // namespace Hyprcast::Overlay
