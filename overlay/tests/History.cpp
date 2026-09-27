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

    void testRuntimeHistoryOptions() {
        InputHistory history({.maxRetainedUtf16CodeUnits = 16});
        history.apply(text(QStringLiteral("abcd")));
        const auto stableId = history.entries().front().id;

        history.setOptions({.backspaceMode = BackspaceMode::Delete, .maxRetainedUtf16CodeUnits = 3});
        check(history.displayText() == QStringLiteral("bcd"), "lowering retention applies immediately at a grapheme boundary");
        check(history.entries().front().id == stableId, "live retention changes preserve surviving entry identity");
        history.setOptions({.backspaceMode = BackspaceMode::Symbol, .maxRetainedUtf16CodeUnits = 16});
        history.apply(key(QStringLiteral("Backspace")));
        check(history.displayText() == QStringLiteral("bcd [Backspace] "), "live Backspace mode changes affect only future actions");
        history.setOptions({.backspaceMode = BackspaceMode::Delete, .maxRetainedUtf16CodeUnits = 16});
        history.apply(key(QStringLiteral("Backspace")));
        history.apply(key(QStringLiteral("Backspace")));
        check(history.displayText() == QStringLiteral("bc"), "switching to deletion mode applies to future actions");
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

    void testStructuredModelRolesAndNotifications() {
        InputHistory history;
        int          inserted = 0;
        int          removed  = 0;
        int          changed  = 0;
        QObject::connect(&history, &QAbstractItemModel::rowsInserted, [&inserted](const QModelIndex&, int, int) { ++inserted; });
        QObject::connect(&history, &QAbstractItemModel::rowsRemoved, [&removed](const QModelIndex&, int, int) { ++removed; });
        QObject::connect(&history, &QAbstractItemModel::dataChanged, [&changed](const QModelIndex&, const QModelIndex&, const QList<int>&) { ++changed; });

        history.apply(text(QStringLiteral("ab"), 9, 30));
        check(history.rowCount() == 1 && inserted == 1, "append inserts a single model row");
        auto textIndex = history.index(0, 0);
        check(history.data(textIndex, HistoryListModel::KindRole).toString() == QStringLiteral("text"), "model exposes the action kind role");
        check(history.data(textIndex, HistoryListModel::TextRole).toString() == QStringLiteral("ab"), "model exposes source text rather than formatted output");
        check(history.data(textIndex, HistoryListModel::KeyboardIdRole).toUInt() == 9, "model exposes keyboard identity");
        const auto firstId = history.data(textIndex, HistoryListModel::EntryIdRole).toULongLong();
        check(history.roleNames().value(HistoryListModel::ModifiersRole) == QByteArrayLiteral("modifiers"), "model publishes QML role names");

        history.apply(key(QStringLiteral("Backspace")));
        check(history.rowCount() == 1 && changed == 1, "grapheme deletion updates a row without resetting the model");
        textIndex = history.index(0, 0);
        check(history.data(textIndex, HistoryListModel::TextRole).toString() == QStringLiteral("a"), "partial text deletion updates structured data");
        check(history.data(textIndex, Qt::DisplayRole).toString() == QStringLiteral("a"), "partial text deletion notifies standard display-role consumers");
        check(history.data(textIndex, HistoryListModel::EntryIdRole).toULongLong() == firstId, "partial deletion preserves the entry ID");

        auto repeatedChord        = chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 42, 46);
        repeatedChord.eventTimeMs = 99;
        repeatedChord.repeated    = true;
        repeatedChord.repeatCount = 2;
        history.apply(repeatedChord);
        const auto chordIndex = history.index(1, 0);
        check(history.data(chordIndex, HistoryListModel::KindRole).toString() == QStringLiteral("chord"), "chord kind is exposed to QML");
        check(history.data(chordIndex, HistoryListModel::ModifiersRole).toStringList() == QStringList{QStringLiteral("Ctrl")}, "chord modifiers remain structured");
        check(history.data(chordIndex, HistoryListModel::KeyRole).toString() == QStringLiteral("C"), "chord key is exposed separately");
        check(history.data(chordIndex, HistoryListModel::KeycodeRole).toUInt() == 46, "key identity is available to presentation models");
        check(history.data(chordIndex, HistoryListModel::EventTimeMsRole).toUInt() == 99 && history.data(chordIndex, HistoryListModel::RepeatedRole).toBool() &&
                  history.data(chordIndex, HistoryListModel::RepeatCountRole).toUInt() == 2,
              "event timing and repeat metadata are exposed without reparsing labels");

        history.apply(key(QStringLiteral("Backspace")));
        check(history.rowCount() == 1 && removed == 1, "atomic chord deletion removes one complete model row");
        history.clear();
        check(history.rowCount() == 0 && removed == 2, "clear reports removal of the remaining model rows");

        history.apply(text(QStringLiteral("e")));
        history.apply(text(QString::fromUtf8("\xCC\x81")));
        history.apply(key(QStringLiteral("Backspace")));
        check(history.rowCount() == 0 && removed == 3, "grapheme deletion across actions removes its contiguous model rows");
    }

    void testCountedRepeatProjectionAndBackspace() {
        InputHistory history({.presentation = {.countedRepeats = true, .repeatThreshold = 4}});
        auto         first = text(QStringLiteral("a"));
        history.apply(first);
        for (std::uint32_t i = 1; i <= 4; ++i) {
            auto repeat        = first;
            repeat.repeated    = true;
            repeat.repeatCount = i;
            history.apply(repeat);
            const QString expected = i < 3 ? QString(i + 1, QLatin1Char('a')) : QStringLiteral("[a x%1] ").arg(i + 1);
            check(history.presentationModel().displayText() == expected, "counted repeats collapse at the configured occurrence threshold");
        }
        check(history.rowCount() == 5 && history.presentationModel().rowCount() == 1, "projection collapses rows without discarding semantic actions");
        check(history.presentationModel().data(history.presentationModel().index(0, 0), HistoryProjectionModel::RepeatCountRole).toUInt() == 5,
              "projected count includes the initial press and generated repeats");

        history.apply(key(QStringLiteral("Backspace")));
        check(history.presentationModel().displayText() == QStringLiteral("[a x4] "), "Backspace decrements the retained counted run");
        history.apply(key(QStringLiteral("Backspace")));
        check(history.presentationModel().displayText() == QStringLiteral("[a x3] "), "a collapsed row stays collapsed below its threshold");
        history.apply(first);
        check(history.presentationModel().displayText() == QStringLiteral("[a x4] "), "a new equivalent tap extends the same group after decrementing it");
        history.apply(key(QStringLiteral("Backspace")));
        history.apply(key(QStringLiteral("Backspace")));
        history.apply(key(QStringLiteral("Backspace")));
        check(history.presentationModel().displayText() == QStringLiteral("a"), "a counted run returns to a plain action at one occurrence");
        history.apply(key(QStringLiteral("Backspace")));
        check(history.presentationModel().displayText().isEmpty(), "Backspace removes the final underlying occurrence");

        InputHistory retained({.maxRetainedUtf16CodeUnits = 4, .presentation = {.countedRepeats = true, .repeatThreshold = 4}});
        auto         retainedAction = text(QStringLiteral("x"));
        for (std::uint32_t i = 0; i < 5; ++i) {
            retainedAction.repeated    = i > 0;
            retainedAction.repeatCount = i;
            retained.apply(retainedAction);
        }
        check(retained.retainedUtf16CodeUnits() == 4 && retained.displayText() == QStringLiteral("xxxx"),
              "retention charges every underlying occurrence rather than the compressed counter");
        check(retained.presentationModel().displayText() == QStringLiteral("[x x4] "), "retention trimming reduces the projected occurrence count");
        retained.apply(key(QStringLiteral("Backspace")));
        check(retained.presentationModel().displayText() == QStringLiteral("[x x3] "), "retention-trimmed groups retain sticky collapse while erasing");
    }

    void testAdjacentCountedInputsAndUnicodeEligibility() {
        InputHistory options({.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
        auto         first = text(QStringLiteral("x"));
        options.apply(first);
        options.apply(text(QStringLiteral("b"), 2, 48));
        auto later        = first;
        later.repeated    = true;
        later.repeatCount = 1;
        options.apply(later);
        check(options.presentationModel().displayText() == QStringLiteral("xbx"), "an interleaved action starts a new counted group");

        InputHistory interleavedRuns({.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
        interleavedRuns.apply(text(QStringLiteral("a"), 1, 30));
        interleavedRuns.apply(text(QStringLiteral("a"), 1, 30));
        interleavedRuns.apply(text(QStringLiteral("b"), 1, 48));
        interleavedRuns.apply(text(QStringLiteral("a"), 1, 30));
        interleavedRuns.apply(text(QStringLiteral("a"), 1, 30));
        check(interleavedRuns.presentationModel().displayText() == QStringLiteral("[a x2] b [a x2] "),
              "matching inputs on opposite sides of another key form separate counted groups");

        InputHistory changed({.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
        auto         changedAction = text(QStringLiteral("x"));
        changed.apply(changedAction);
        changedAction.text        = QStringLiteral("y");
        changedAction.repeated    = true;
        changedAction.repeatCount = 1;
        changed.apply(changedAction);
        changedAction.repeatCount = 2;
        changed.apply(changedAction);
        check(changed.presentationModel().displayText() == QStringLiteral("x [y x2] "), "a changed interpretation starts a distinct counted group");

        InputHistory mixed({.presentation = {.countedRepeats = true, .repeatThreshold = 3}});
        auto         manualFirst = text(QStringLiteral("m"), 1, 50);
        manualFirst.eventTimeMs  = 1;
        mixed.apply(manualFirst);
        auto manualTap        = manualFirst;
        manualTap.eventTimeMs = 600000;
        mixed.apply(manualTap);
        auto generatedRepeat        = manualTap;
        generatedRepeat.repeated    = true;
        generatedRepeat.repeatCount = 1;
        generatedRepeat.eventTimeMs = 600001;
        mixed.apply(generatedRepeat);
        check(mixed.presentationModel().displayText() == QStringLiteral("[m x3] "), "manual taps and generated repeats share one no-timeout equivalent-input group");

        InputHistory separate({.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
        auto         tapA = text(QStringLiteral("a"), 1, 30);
        tapA.eventTimeMs  = 1;
        auto tapB         = tapA;
        tapB.eventTimeMs  = 900000;
        separate.apply(tapA);
        separate.apply(tapB);
        check(separate.presentationModel().displayText() == QStringLiteral("[a x2] "), "same-key taps count together regardless of the gap between them");

        InputHistory crossKeyboard({.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
        crossKeyboard.apply(text(QStringLiteral("a"), 1, 30));
        crossKeyboard.apply(text(QStringLiteral("a"), 2, 30));
        check(crossKeyboard.presentationModel().displayText() == QStringLiteral("aa"), "matching inputs from different keyboards remain separate");

        InputHistory chords({.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
        chords.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 1, 46));
        chords.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 1, 46));
        check(chords.presentationModel().displayText() == QStringLiteral("[Ctrl+C x2] "), "identical chord taps combine across release and repress");

        InputHistory specialKeys({.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
        specialKeys.apply(key(QStringLiteral("Enter"), 1, 28));
        specialKeys.apply(key(QStringLiteral("Enter"), 1, 28));
        check(specialKeys.presentationModel().displayText() == QStringLiteral("[Enter x2] "), "identical special-key taps combine across release and repress");

        HistoryPresentationOptions sharedGlyphs;
        sharedGlyphs.countedRepeats  = true;
        sharedGlyphs.repeatThreshold = 2;
        sharedGlyphs.keySymbols      = {{QStringLiteral("A"), QStringLiteral("★")}, {QStringLiteral("B"), QStringLiteral("★")}};
        sharedGlyphs.modifierSymbols = {{QStringLiteral("Ctrl"), QStringLiteral("M")}, {QStringLiteral("Shift"), QStringLiteral("M")}};
        InputHistory canonicalKeys({.presentation = sharedGlyphs});
        canonicalKeys.apply(key(QStringLiteral("A"), 1, 30));
        canonicalKeys.apply(key(QStringLiteral("B"), 1, 48));
        canonicalKeys.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 1, 46));
        canonicalKeys.apply(chord({QStringLiteral("Shift")}, QStringLiteral("C"), 1, 46));
        check(canonicalKeys.presentationModel().displayText() == QStringLiteral("[★] [★] [M+C] [M+C] "),
              "canonical keys and modifiers remain distinct even when symbol mappings render them identically");

        InputHistory deletedIntervening({.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
        deletedIntervening.apply(text(QStringLiteral("a"), 1, 30));
        deletedIntervening.apply(text(QStringLiteral("a"), 1, 30));
        deletedIntervening.apply(text(QStringLiteral("b"), 1, 48));
        deletedIntervening.apply(key(QStringLiteral("Backspace")));
        deletedIntervening.apply(text(QStringLiteral("a"), 1, 30));
        check(deletedIntervening.presentationModel().displayText() == QStringLiteral("[a x2] a"),
              "deleting an intervening input does not retroactively merge two previously separate groups");

        InputHistory multiGrapheme({.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
        auto         twoChars = text(QStringLiteral("ab"));
        multiGrapheme.apply(twoChars);
        twoChars.repeated    = true;
        twoChars.repeatCount = 1;
        multiGrapheme.apply(twoChars);
        check(multiGrapheme.presentationModel().displayText() == QStringLiteral("abab"), "multi-grapheme actions remain expanded rather than corrupting deletion semantics");

        InputHistory joined({.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
        joined.apply(text(QStringLiteral("e")));
        auto combining = text(QString::fromUtf8("\xCC\x81"));
        joined.apply(combining);
        combining.repeated    = true;
        combining.repeatCount = 1;
        joined.apply(combining);
        check(joined.presentationModel().displayText() == QString::fromUtf8("e\xCC\x81\xCC\x81"), "graphemes spanning action boundaries prevent unsafe count projection");
        joined.apply(key(QStringLiteral("Backspace")));
        check(joined.displayText().isEmpty(), "Unicode deletion still removes one grapheme across source actions");
    }

    void testSymbolProjectionAndRetentionAccounting() {
        HistoryPresentationOptions presentation;
        presentation.symbolFontFamily = QStringLiteral("Symbols & Friends");
        presentation.keySymbols       = {{QStringLiteral("Backspace"), QStringLiteral("⌫")}, {QStringLiteral("C"), QStringLiteral("COPY")}};
        presentation.modifierSymbols  = {{QStringLiteral("Ctrl"), QStringLiteral("CTRL")}};
        InputHistory history({.backspaceMode = BackspaceMode::Symbol, .maxRetainedUtf16CodeUnits = 64, .presentation = presentation});
        history.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("C")));
        history.apply(key(QStringLiteral("Backspace")));
        check(history.displayText() == QStringLiteral("[Ctrl+C] [Backspace] "), "canonical raw labels remain available and unchanged");
        check(history.presentationModel().displayText() == QStringLiteral("[CTRL+COPY] [⌫] "), "configured key and modifier labels are projected consistently");
        check(history.entries()[0].action.key == QStringLiteral("C") && history.entries()[0].action.modifiers == QStringList{QStringLiteral("Ctrl")},
              "symbol substitutions do not alter canonical key or modifier identity");
        check(history.presentationModel().displayRichText().contains(QStringLiteral("Symbols &amp; Friends")), "rich text escapes configured symbol font names");

        const qsizetype before = history.retainedUtf16CodeUnits();
        presentation.keySymbols.insert(QStringLiteral("C"), QStringLiteral("a much longer presentation-only label"));
        history.setOptions({.backspaceMode = BackspaceMode::Symbol, .maxRetainedUtf16CodeUnits = 64, .presentation = presentation});
        check(history.retainedUtf16CodeUnits() == before, "symbol changes do not change the stable canonical retention budget");

        InputHistory deletion({.presentation = presentation});
        deletion.apply(text(QStringLiteral("abc")));
        deletion.apply(key(QStringLiteral("Backspace")));
        check(deletion.displayText() == QStringLiteral("ab"), "a Backspace symbol mapping cannot alter deletion identity");

        InputHistory backspaceSymbols({.backspaceMode = BackspaceMode::Symbol,
                                       .presentation  = {.countedRepeats = true, .repeatThreshold = 3, .keySymbols = {{QStringLiteral("Backspace"), QStringLiteral("⌫")}}}});
        auto         backspace = key(QStringLiteral("Backspace"));
        backspaceSymbols.apply(backspace);
        for (std::uint32_t i = 1; i <= 2; ++i) {
            backspace.repeated    = true;
            backspace.repeatCount = i;
            backspaceSymbols.apply(backspace);
        }
        check(backspaceSymbols.presentationModel().displayText() == QStringLiteral("[⌫ x3] "), "symbol-mode repeated Backspace is grouped and uses its configured display glyph");
    }

    void testProjectionPreservesStableTailIdsDuringRetention() {
        InputHistory history({.maxRetainedUtf16CodeUnits = 3});
        history.apply(text(QStringLiteral("a")));
        history.apply(text(QStringLiteral("b")));
        history.apply(text(QStringLiteral("c")));
        auto&      projection   = history.presentationModel();
        const auto middleId     = projection.data(projection.index(1, 0), HistoryProjectionModel::EntryIdRole).toULongLong();
        const auto tailId       = projection.data(projection.index(2, 0), HistoryProjectionModel::EntryIdRole).toULongLong();
        int        removedFirst = -1;
        QObject::connect(&projection, &QAbstractItemModel::rowsRemoved, [&removedFirst](const QModelIndex&, int first, int) { removedFirst = first; });
        history.apply(text(QStringLiteral("d")));
        check(projection.rowCount() == 3 && projection.data(projection.index(0, 0), HistoryProjectionModel::EntryIdRole).toULongLong() == middleId &&
                  projection.data(projection.index(1, 0), HistoryProjectionModel::EntryIdRole).toULongLong() == tailId,
              "retention preserves stable IDs for surviving projected tail rows");
        check(removedFirst == 0 && projection.data(projection.index(2, 0), HistoryProjectionModel::TextRole).toString() == QStringLiteral("d"),
              "projection emits precise front-removal and tail-insertion notifications during retention");
    }

    void testProjectionNotificationsAndSnapshot() {
        InputHistory history({.presentation = {.countedRepeats = true, .repeatThreshold = 3}});
        int          inserted   = 0;
        int          removed    = 0;
        int          changed    = 0;
        auto&        projection = history.presentationModel();
        QObject::connect(&projection, &QAbstractItemModel::rowsInserted, [&inserted](const QModelIndex&, int, int) { ++inserted; });
        QObject::connect(&projection, &QAbstractItemModel::rowsRemoved, [&removed](const QModelIndex&, int, int) { ++removed; });
        QObject::connect(&projection, &QAbstractItemModel::dataChanged, [&changed](const QModelIndex&, const QModelIndex&, const QList<int>&) { ++changed; });

        auto action = text(QStringLiteral("z"));
        history.apply(action);
        action.repeated    = true;
        action.repeatCount = 1;
        history.apply(action);
        action.repeatCount = 2;
        history.apply(action);
        const auto groupId = projection.data(projection.index(0, 0), HistoryProjectionModel::EntryIdRole).toULongLong();
        check(projection.rowCount() == 1 && removed == 1 && changed == 1, "crossing the threshold sends row-removal and data-change notifications");
        check(projection.data(projection.index(0, 0), HistoryProjectionModel::EntryIdRole).toULongLong() == groupId,
              "projected group identity stays anchored to its first retained semantic action");

        HistoryListModel snapshot;
        snapshot.setSnapshot(history.entries(), history.presentationOptions(), history.collapsedRepeatRuns());
        check(snapshot.presentationModel().displayText() == QStringLiteral("[z x3] "), "expiration-style snapshots preserve counted projection state");
        check(inserted >= 2, "projection emits insert notifications for expanded semantic rows");
    }

    void testRetentionModelNotificationsAndSnapshot() {
        InputHistory     history({.maxRetainedUtf16CodeUnits = 5});
        HistoryListModel snapshot;
        int              changed = 0;
        QObject::connect(&history, &QAbstractItemModel::dataChanged, [&changed](const QModelIndex&, const QModelIndex&, const QList<int>&) { ++changed; });

        history.apply(text(QStringLiteral("abcdefg")));
        const auto retainedId = history.data(history.index(0, 0), HistoryListModel::EntryIdRole).toULongLong();
        check(history.displayText() == QStringLiteral("cdefg"), "retention truncates the structured text row at a grapheme boundary");
        check(history.data(history.index(0, 0), HistoryListModel::EntryIdRole).toULongLong() == retainedId, "retention preserves the surviving text entry ID");
        snapshot.setSnapshot(history.entries());
        history.clear();
        check(history.rowCount() == 0 && snapshot.rowCount() == 1, "visual snapshots are independent of editable history");
        check(snapshot.displayText() == QStringLiteral("cdefg"), "snapshot model uses the same rendering semantics");
        snapshot.clear();
        check(changed == 1, "partial retention trimming emits dataChanged rather than a model reset");

        InputHistory multipleRows({.maxRetainedUtf16CodeUnits = 4});
        int          trimRemovals = 0;
        QObject::connect(&multipleRows, &QAbstractItemModel::rowsRemoved, [&trimRemovals](const QModelIndex&, int, int) { ++trimRemovals; });
        multipleRows.apply(text(QStringLiteral("ab")));
        multipleRows.apply(text(QStringLiteral("cd")));
        const auto survivingId = multipleRows.entries()[1].id;
        multipleRows.apply(text(QStringLiteral("ef")));
        check(multipleRows.displayText() == QStringLiteral("cdef"), "retention can remove complete old action rows");
        check(multipleRows.rowCount() == 2 && multipleRows.entries().front().id == survivingId && trimRemovals == 1,
              "retention reports removed rows and preserves IDs of surviving actions");
    }
} // namespace

int main() {
    testBackspaceModesAndRepeatedDeletion();
    testRuntimeHistoryOptions();
    testAtomicSpecialKeysAndModifiedBackspace();
    testGraphemesAcrossActions();
    testBoundedRetentionAndStableMetadata();
    testMixedKeyboardOrderAndEntryIdentity();
    testStructuredModelRolesAndNotifications();
    testCountedRepeatProjectionAndBackspace();
    testAdjacentCountedInputsAndUnicodeEligibility();
    testSymbolProjectionAndRetentionAccounting();
    testProjectionPreservesStableTailIdsDuringRetention();
    testProjectionNotificationsAndSnapshot();
    testRetentionModelNotificationsAndSnapshot();
    std::cout << "overlay history tests passed\n";
    return 0;
}
