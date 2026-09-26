#include "history/InputHistory.hpp"

#include <cstdlib>
#include <iostream>
#include <utility>

using namespace Hyprcast::Overlay;

namespace {
    void check(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "check failed: " << message << '\n';
            std::exit(1);
        }
    }

    InterpretedAction text(QString value, std::uint32_t keyboardId = 1, std::uint32_t keycode = 0) {
        InterpretedAction action;
        action.kind       = InterpretedActionKind::Text;
        action.keyboardId = keyboardId;
        action.keycode    = keycode;
        action.text       = std::move(value);
        return action;
    }

    InterpretedAction key(QString label, std::uint32_t keyboardId = 1, std::uint32_t keycode = 0) {
        InterpretedAction action;
        action.kind       = InterpretedActionKind::Key;
        action.keyboardId = keyboardId;
        action.keycode    = keycode;
        action.key        = std::move(label);
        return action;
    }

    InterpretedAction chord(QStringList modifiers, QString label, std::uint32_t keyboardId = 1, std::uint32_t keycode = 0) {
        InterpretedAction action;
        action.kind       = InterpretedActionKind::Chord;
        action.keyboardId = keyboardId;
        action.keycode    = keycode;
        action.key        = std::move(label);
        action.modifiers  = std::move(modifiers);
        return action;
    }

    void testBackspaceModesAndRepeatedDeletion() {
        InputHistory history;
        check(!history.apply(key(QStringLiteral("Backspace"))), "Backspace on empty history is a no-op");

        history.apply(text(QStringLiteral("hello")));
        history.apply(key(QStringLiteral("Backspace")));
        check(history.displayText() == QStringLiteral("hell"), "default Backspace deletes one grapheme");

        auto repeated        = key(QStringLiteral("Backspace"));
        repeated.repeated    = true;
        repeated.repeatCount = 1;
        history.apply(repeated);
        repeated.repeatCount = 2;
        history.apply(repeated);
        check(history.displayText() == QStringLiteral("he"), "repeated Backspace actions repeatedly delete");

        InputHistory emptySymbols({.backspaceMode = BackspaceMode::Symbol});
        emptySymbols.apply(key(QStringLiteral("Backspace")));
        check(emptySymbols.displayText() == QStringLiteral("[Backspace] "), "symbol mode records Backspace even on empty history");

        InputHistory symbols({.backspaceMode = BackspaceMode::Symbol});
        symbols.apply(text(QStringLiteral("x")));
        symbols.apply(key(QStringLiteral("Backspace")));
        check(symbols.displayText() == QStringLiteral("x [Backspace] "), "symbol mode records Backspace as a visible key");
        symbols.apply(text(QStringLiteral("y")));
        check(symbols.displayText() == QStringLiteral("x [Backspace] y"), "rendered separators are derived around typed entries");
    }

    void testAtomicSpecialKeysAndModifiedBackspace() {
        InputHistory history;
        history.apply(text(QStringLiteral("a")));
        history.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 2, 46));
        history.apply(key(QStringLiteral("Enter"), 3, 28));
        check(history.displayText() == QStringLiteral("a [Ctrl+C] [Enter] "), "mixed entries render with synthetic separators");

        auto repeatedBackspace        = key(QStringLiteral("Backspace"));
        repeatedBackspace.repeated    = true;
        repeatedBackspace.repeatCount = 1;
        history.apply(repeatedBackspace);
        check(history.displayText() == QStringLiteral("a [Ctrl+C] "), "one repeated Backspace removes an entire special-key entry");
        repeatedBackspace.repeatCount = 2;
        history.apply(repeatedBackspace);
        check(history.displayText() == QStringLiteral("a"), "repeated Backspace removes a chord without leftover formatting");

        history.apply(text(QStringLiteral("bc")));
        history.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("Backspace")));
        check(history.displayText() == QStringLiteral("abc [Ctrl+Backspace] "), "modified Backspace is displayed as a chord, not word deletion");
        history.apply(key(QStringLiteral("Backspace")));
        check(history.displayText() == QStringLiteral("abc"), "plain Backspace then deletes the modified chord atomically");
    }

    void testGraphemesAcrossActions() {
        InputHistory history;
        history.apply(text(QStringLiteral("e"), 1, 18));
        history.apply(text(QString::fromUtf8("\xCC\x81"), 2, 40));
        check(history.displayText() == QString::fromUtf8("e\xCC\x81"), "combining mark actions remain contiguous");
        check(history.entries().size() == 2, "each text action retains its own history identity");
        history.apply(key(QStringLiteral("Backspace")));
        check(history.displayText().isEmpty() && history.entries().empty(), "Backspace erases one grapheme even across action boundaries");

        const char32_t womanCodepoint[]  = {0x1F469};
        const char32_t laptopCodepoint[] = {0x1F4BB};
        history.apply(text(QString::fromUcs4(womanCodepoint, 1)));
        history.apply(text(QString(QChar(0x200D))));
        history.apply(text(QString::fromUcs4(laptopCodepoint, 1)));
        history.apply(key(QStringLiteral("Backspace")));
        check(history.displayText().isEmpty(), "Backspace erases a multi-codepoint emoji grapheme");
    }

    void testBoundedRetentionAndStableMetadata() {
        InputHistory history({.maxRetainedUtf16CodeUnits = 5});
        auto         original = text(QStringLiteral("abcdefg"), 42, 30);
        history.apply(original);
        check(history.displayText() == QStringLiteral("cdefg"), "retention keeps the newest content under its UTF-16 display budget");
        check(history.retainedUtf16CodeUnits() == 5, "retained size is measured in rendered UTF-16 code units");
        check(history.entries().size() == 1 && history.entries().front().id == 1, "trimmed text keeps its stable entry identity");
        check(history.entries().front().action.keyboardId == 42 && history.entries().front().action.keycode == 30, "history entries preserve keyboard and key identity");

        history.apply(text(QStringLiteral("h"), 99, 31));
        check(history.displayText() == QStringLiteral("defgh"), "new content evicts oldest retained text at the fixed bound");
        history.apply(key(QStringLiteral("Backspace")));
        check(history.displayText() == QStringLiteral("defg"), "deletion reveals retained content but cannot recover evicted content");

        InputHistory unicode({.maxRetainedUtf16CodeUnits = 3});
        unicode.apply(text(QStringLiteral("a😀b")));
        check(unicode.displayText() == QStringLiteral("😀b"), "retention trims at a grapheme boundary using UTF-16 code-unit accounting");
        check(unicode.retainedUtf16CodeUnits() == 3, "non-BMP characters count as two UTF-16 code units");
    }

    void testMixedKeyboardOrderAndEntryIdentity() {
        InputHistory history;
        history.apply(text(QStringLiteral("a"), 11, 30));
        history.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 22, 46));
        history.apply(text(QStringLiteral("b"), 33, 48));

        const auto& entries = history.entries();
        check(entries.size() == 3, "mixed actions remain separate ordered entries");
        check(entries[0].id < entries[1].id && entries[1].id < entries[2].id, "entry IDs are stable and monotonic");
        check(entries[0].action.keyboardId == 11 && entries[1].action.keyboardId == 22 && entries[2].action.keyboardId == 33, "mixed keyboard identity is preserved per entry");
        check(history.displayText() == QStringLiteral("a [Ctrl+C] b"), "mixed-keyboard action order is preserved in display output");
    }
} // namespace

int main() {
    testBackspaceModesAndRepeatedDeletion();
    testAtomicSpecialKeysAndModifiedBackspace();
    testGraphemesAcrossActions();
    testBoundedRetentionAndStableMetadata();
    testMixedKeyboardOrderAndEntryIdentity();
    std::cout << "overlay history tests passed\n";
    return 0;
}
