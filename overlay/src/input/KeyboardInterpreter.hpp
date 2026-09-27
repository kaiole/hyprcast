#pragma once

#include "../ipc/Protocol.hpp"

#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace Hyprcast::Overlay {
    enum class InterpretedActionKind {
        Text,
        Key,
        Chord,
    };

    struct HeldKey {
        QString kind;
        QString identity;
        QString text;
    };

    struct InterpretedAction {
        InterpretedActionKind                 kind        = InterpretedActionKind::Key;
        std::uint32_t                         keyboardId  = 0;
        std::uint32_t                         keycode     = 0; // Hyprland's evdev keycode, before the XKB +8 offset.
        std::uint32_t                         eventTimeMs = 0;
        QString                               text;
        QString                               key;
        QStringList                           modifiers;
        bool                                  repeated    = false;
        std::uint32_t                         repeatCount = 0;
        std::chrono::steady_clock::time_point generatedAt{};
    };

    class KeyboardInterpreter final {
      public:
        using Clock = std::chrono::steady_clock;

        KeyboardInterpreter();
        ~KeyboardInterpreter();
        KeyboardInterpreter(KeyboardInterpreter&&) noexcept;
        KeyboardInterpreter& operator=(KeyboardInterpreter&&) noexcept;
        KeyboardInterpreter(const KeyboardInterpreter&)                                    = delete;
        KeyboardInterpreter&                         operator=(const KeyboardInterpreter&) = delete;

        [[nodiscard]] std::vector<InterpretedAction> process(const ProtocolMessage& message, Clock::time_point now, QString* error = nullptr);
        [[nodiscard]] std::vector<InterpretedAction> advance(Clock::time_point now);
        [[nodiscard]] Clock::time_point              nextRepeatDeadline() const noexcept;
        [[nodiscard]] QString                        heldModifiers() const;
        [[nodiscard]] QStringList                    heldKeys() const;
        [[nodiscard]] std::vector<HeldKey>           heldKeyItems() const;
        void                                         setRepeatsEnabled(bool enabled, Clock::time_point now = Clock::now());
        void                                         reset() noexcept;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Hyprcast::Overlay
