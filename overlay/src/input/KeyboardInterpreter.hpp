#pragma once

#include "../history/InterpretedAction.hpp"
#include "../ipc/Protocol.hpp"

#include <QString>
#include <QStringList>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <vector>

namespace hyprcast::overlay {

struct HeldKey {
    enum class Kind : std::uint8_t {
        Modifier,
        Text,
        Key,
    };

    Kind kind = Kind::Key;
    QString identity {}; // Modifier or key label; empty for Text.
    QString text {};     // Produced text; Text only.

    [[nodiscard]] const QString& label() const noexcept {
        return kind == Kind::Text ? text : identity;
    }
    friend bool operator==(const HeldKey&, const HeldKey&) = default;
};

class KeyboardInterpreter final {
public:
    using Clock = std::chrono::steady_clock;

    KeyboardInterpreter();
    // Defined out of line, where Impl is complete (required for the pimpl's
    // unique_ptr).
    ~KeyboardInterpreter();
    // Never moved, so no moved-from (null m_impl) state needs to be handled.
    KeyboardInterpreter(const KeyboardInterpreter&) = delete;
    KeyboardInterpreter& operator=(const KeyboardInterpreter&) = delete;
    KeyboardInterpreter(KeyboardInterpreter&&) = delete;
    KeyboardInterpreter& operator=(KeyboardInterpreter&&) = delete;

    [[nodiscard]] std::expected<std::vector<InterpretedAction>, QString>
    process(const ProtocolMessage& message, Clock::time_point now);
    [[nodiscard]] std::vector<InterpretedAction> advance(Clock::time_point now);
    [[nodiscard]] Clock::time_point nextRepeatDeadline() const noexcept;
    [[nodiscard]] std::vector<HeldKey> heldKeyItems() const;
    void setRepeatsEnabled(bool enabled, Clock::time_point now = Clock::now());
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hyprcast::overlay
