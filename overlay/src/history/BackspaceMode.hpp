#pragma once

#include <cstdint>

namespace Hyprcast::Overlay {

// What plain Backspace does to history.
enum class BackspaceMode : std::uint8_t {
    Delete, // Erases the previous grapheme or entry.
    Symbol, // Records Backspace like any other key.
};

} // namespace Hyprcast::Overlay
