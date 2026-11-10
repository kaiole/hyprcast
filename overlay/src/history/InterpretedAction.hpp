#pragma once

#include <QLatin1StringView>

#include <QString>
#include <QStringList>
#include <QStringView>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>

namespace Hyprcast::Overlay {

// Modifier identities: the values in InterpretedAction::modifiers, the key of a
// lone modifier tap, and the keys of [symbols.modifiers]. Defined once so the
// interpreter that produces them and the history that maps them cannot drift.
namespace Modifier {

inline constexpr QLatin1StringView Shift {"Shift"};
inline constexpr QLatin1StringView Ctrl {"Ctrl"};
inline constexpr QLatin1StringView Alt {"Alt"};
inline constexpr QLatin1StringView Super {"Super"};
inline constexpr QLatin1StringView AltGr {"AltGr"};
inline constexpr QLatin1StringView Meta {"Meta"};
inline constexpr QLatin1StringView Hyper {"Hyper"};

inline constexpr std::array All {Shift, Ctrl, Alt, Super, AltGr, Meta, Hyper};

[[nodiscard]] inline bool isIdentity(QStringView identity) {
    return std::ranges::any_of(All, [identity](QLatin1StringView modifier) {
        return identity == modifier;
    });
}

} // namespace Modifier

enum class InterpretedActionKind {
    Text,
    Key,
    Chord,
};

struct InterpretedAction {
    InterpretedActionKind kind = InterpretedActionKind::Key;
    std::uint32_t keyboardId = 0;
    std::uint32_t keycode =
        0; // Hyprland's evdev keycode, before the XKB +8 offset.
    std::uint32_t eventTimeMs = 0;
    QString text;
    QString key;
    QStringList modifiers;
    bool repeated = false;
    std::uint32_t repeatCount = 0;
    std::chrono::steady_clock::time_point generatedAt {};
};

} // namespace Hyprcast::Overlay
