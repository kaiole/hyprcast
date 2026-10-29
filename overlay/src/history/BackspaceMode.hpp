#pragma once

namespace Hyprcast::Overlay {
    // What plain Backspace does to history.
    enum class BackspaceMode {
        Delete, // Erases the previous grapheme or entry.
        Symbol, // Records Backspace like any other key.
    };
}
