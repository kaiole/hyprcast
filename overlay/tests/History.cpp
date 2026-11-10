#include "Support.hpp"
#include "history/BackspaceMode.hpp"
#include "history/InputHistory.hpp"
#include "history/InterpretedAction.hpp"

#include <gtest/gtest.h>

#include <QAbstractItemModel>
#include <QByteArray>
#include <QChar>
#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QString>
#include <cstdint>
#include <utility>

using namespace hyprcast::overlay;

namespace {

InterpretedAction
text(QString value, std::uint32_t keyboardId = 1, std::uint32_t keycode = 0) {
    InterpretedAction action;
    action.kind = InterpretedActionKind::Text;
    action.keyboardId = keyboardId;
    action.keycode = keycode;
    action.text = std::move(value);
    return action;
}

InterpretedAction
key(QString label, std::uint32_t keyboardId = 1, std::uint32_t keycode = 0) {
    InterpretedAction action;
    action.kind = InterpretedActionKind::Key;
    action.keyboardId = keyboardId;
    action.keycode = keycode;
    action.key = std::move(label);
    return action;
}

InterpretedAction chord(QStringList modifiers,
                        QString label,
                        std::uint32_t keyboardId = 1,
                        std::uint32_t keycode = 0) {
    InterpretedAction action;
    action.kind = InterpretedActionKind::Chord;
    action.keyboardId = keyboardId;
    action.keycode = keycode;
    action.key = std::move(label);
    action.modifiers = std::move(modifiers);
    return action;
}

TEST(HistoryEditing, BackspaceDeletesOneGraphemePerAction) {
    InputHistory history;
    ASSERT_FALSE(history.apply(key(QStringLiteral("Backspace"))))
        << "Backspace on empty history is a no-op";

    history.apply(text(QStringLiteral("hello")));
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(history.displayText(), QStringLiteral("hell"))
        << "default Backspace deletes one grapheme";

    auto repeated = key(QStringLiteral("Backspace"));
    repeated.repeated = true;
    repeated.repeatCount = 1;
    history.apply(repeated);
    repeated.repeatCount = 2;
    history.apply(repeated);
    ASSERT_EQ(history.displayText(), QStringLiteral("he"))
        << "repeated Backspace actions repeatedly delete";
}

TEST(HistoryEditing, SymbolModeRecordsBackspaceAsAKey) {
    InputHistory emptySymbols({.backspaceMode = BackspaceMode::Symbol});
    emptySymbols.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(emptySymbols.displayText(), QStringLiteral("[Backspace] "))
        << "symbol mode records Backspace even on empty history";

    InputHistory symbols({.backspaceMode = BackspaceMode::Symbol});
    symbols.apply(text(QStringLiteral("x")));
    symbols.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(symbols.displayText(), QStringLiteral("x [Backspace] "))
        << "symbol mode records Backspace as a visible key";
    symbols.apply(text(QStringLiteral("y")));
    ASSERT_EQ(symbols.displayText(), QStringLiteral("x [Backspace] y"))
        << "rendered separators are derived around typed entries";
}

TEST(HistoryEditing, OptionsApplyAtRuntime) {
    InputHistory history({.maxRetainedUtf16CodeUnits = 16});
    history.apply(text(QStringLiteral("abcd")));
    const auto stableId = history.entries().front().id;

    history.setOptions({
        .backspaceMode = BackspaceMode::Delete,
        .maxRetainedUtf16CodeUnits = 3,
    });
    ASSERT_EQ(history.displayText(), QStringLiteral("bcd"))
        << "lowering retention applies immediately at a grapheme boundary";
    ASSERT_EQ(history.entries().front().id, stableId)
        << "live retention changes preserve surviving entry identity";
    history.setOptions({
        .backspaceMode = BackspaceMode::Symbol,
        .maxRetainedUtf16CodeUnits = 16,
    });
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(history.displayText(), QStringLiteral("bcd [Backspace] "))
        << "live Backspace mode changes affect only future actions";
    history.setOptions({
        .backspaceMode = BackspaceMode::Delete,
        .maxRetainedUtf16CodeUnits = 16,
    });
    history.apply(key(QStringLiteral("Backspace")));
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(history.displayText(), QStringLiteral("bc"))
        << "switching to deletion mode applies to future actions";
}

TEST(HistoryEditing, BackspaceRemovesASpecialKeyWhole) {
    InputHistory history;
    history.apply(text(QStringLiteral("a")));
    history.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 2, 46));
    history.apply(key(QStringLiteral("Enter"), 3, 28));
    ASSERT_EQ(history.displayText(), QStringLiteral("a [Ctrl+C] [Enter] "))
        << "mixed entries render with synthetic separators";

    auto repeatedBackspace = key(QStringLiteral("Backspace"));
    repeatedBackspace.repeated = true;
    repeatedBackspace.repeatCount = 1;
    history.apply(repeatedBackspace);
    ASSERT_EQ(history.displayText(), QStringLiteral("a [Ctrl+C] "))
        << "one repeated Backspace removes an entire special-key entry";
    repeatedBackspace.repeatCount = 2;
    history.apply(repeatedBackspace);
    ASSERT_EQ(history.displayText(), QStringLiteral("a"))
        << "repeated Backspace removes a chord without leftover formatting";

    history.apply(text(QStringLiteral("bc")));
    history.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("Backspace")));
    ASSERT_EQ(history.displayText(), QStringLiteral("abc [Ctrl+Backspace] "))
        << "modified Backspace is displayed as a chord, not word deletion";
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(history.displayText(), QStringLiteral("abc"))
        << "plain Backspace then deletes the modified chord atomically";
}

TEST(HistoryEditing, BackspaceErasesAGraphemeAcrossActions) {
    InputHistory history;
    history.apply(text(QStringLiteral("e"), 1, 18));
    history.apply(text(QString::fromUtf8("\xCC\x81"), 2, 40));
    ASSERT_EQ(history.displayText(), QString::fromUtf8("e\xCC\x81"))
        << "combining mark actions remain contiguous";
    ASSERT_EQ(history.entries().size(), 2)
        << "each text action retains its own history identity";
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_TRUE(history.displayText().isEmpty() && history.entries().empty())
        << "Backspace erases one grapheme even across action boundaries";

    history.apply(text(QString::fromUcs4(U"\U0001F469")));
    history.apply(text(QString(QChar(0x200D))));
    history.apply(text(QString::fromUcs4(U"\U0001F4BB")));
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_TRUE(history.displayText().isEmpty())
        << "Backspace erases a multi-codepoint emoji grapheme";
}

TEST(HistoryRetention, KeepsTheNewestContentAndItsMetadata) {
    InputHistory history({.maxRetainedUtf16CodeUnits = 5});
    const auto original = text(QStringLiteral("abcdefg"), 42, 30);
    history.apply(original);
    ASSERT_EQ(history.displayText(), QStringLiteral("cdefg"))
        << "retention keeps the newest content under its UTF-16 display budget";
    ASSERT_EQ(history.retainedUtf16CodeUnits(), 5)
        << "retained size is measured in rendered UTF-16 code units";
    ASSERT_TRUE(history.entries().size() == 1
                && history.entries().front().id == 1)
        << "trimmed text keeps its stable entry identity";
    ASSERT_TRUE(history.entries().front().action.keyboardId == 42
                && history.entries().front().action.keycode == 30)
        << "history entries preserve keyboard and key identity";

    history.apply(text(QStringLiteral("h"), 99, 31));
    ASSERT_EQ(history.displayText(), QStringLiteral("defgh"))
        << "new content evicts oldest retained text at the fixed bound";
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(history.displayText(), QStringLiteral("defg"))
        << "deletion reveals retained content but cannot recover evicted "
           "content";
}

TEST(HistoryRetention, TrimsAtAGraphemeBoundary) {
    InputHistory unicode({.maxRetainedUtf16CodeUnits = 3});
    unicode.apply(text(QStringLiteral("a😀b")));
    ASSERT_EQ(unicode.displayText(), QStringLiteral("😀b"))
        << "retention trims at a grapheme boundary using UTF-16 code-unit "
           "accounting";
    ASSERT_EQ(unicode.retainedUtf16CodeUnits(), 3)
        << "non-BMP characters count as two UTF-16 code units";
}

TEST(HistoryEditing, KeepsOrderAndIdentityAcrossKeyboards) {
    InputHistory history;
    history.apply(text(QStringLiteral("a"), 11, 30));
    history.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 22, 46));
    history.apply(text(QStringLiteral("b"), 33, 48));

    const auto& entries = history.entries();
    ASSERT_EQ(entries.size(), 3)
        << "mixed actions remain separate ordered entries";
    ASSERT_TRUE(entries[0].id < entries[1].id && entries[1].id < entries[2].id)
        << "entry IDs are stable and monotonic";
    ASSERT_TRUE(entries[0].action.keyboardId == 11
                && entries[1].action.keyboardId == 22
                && entries[2].action.keyboardId == 33)
        << "mixed keyboard identity is preserved per entry";
    ASSERT_EQ(history.displayText(), QStringLiteral("a [Ctrl+C] b"))
        << "mixed-keyboard action order is preserved in display output";
}

TEST(HistoryModel, ExposesRolesAndNotifiesChanges) {
    InputHistory history;
    int inserted = 0;
    int removed = 0;
    int changed = 0;
    QObject::connect(&history,
                     &QAbstractItemModel::rowsInserted,
                     [&inserted](const QModelIndex&, int, int) { ++inserted; });
    QObject::connect(&history,
                     &QAbstractItemModel::rowsRemoved,
                     [&removed](const QModelIndex&, int, int) { ++removed; });
    QObject::connect(&history,
                     &QAbstractItemModel::dataChanged,
                     [&changed](const QModelIndex&,
                                const QModelIndex&,
                                const QList<int>&) { ++changed; });

    history.apply(text(QStringLiteral("ab"), 9, 30));
    ASSERT_TRUE(history.rowCount() == 1 && inserted == 1)
        << "append inserts a single model row";
    auto textIndex = history.index(0, 0);
    ASSERT_EQ(history.data(textIndex, HistoryListModel::KindRole).toString(),
              QStringLiteral("text"))
        << "model exposes the action kind role";
    ASSERT_EQ(history.data(textIndex, HistoryListModel::TextRole).toString(),
              QStringLiteral("ab"))
        << "model exposes source text rather than formatted output";
    ASSERT_EQ(
        history.data(textIndex, HistoryListModel::KeyboardIdRole).toUInt(),
        9)
        << "model exposes keyboard identity";
    const auto firstId =
        history.data(textIndex, HistoryListModel::EntryIdRole).toULongLong();
    ASSERT_EQ(history.roleNames().value(HistoryListModel::ModifiersRole),
              QByteArrayLiteral("modifiers"))
        << "model publishes QML role names";

    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_TRUE(history.rowCount() == 1 && changed == 1)
        << "grapheme deletion updates a row without resetting the model";
    textIndex = history.index(0, 0);
    ASSERT_EQ(history.data(textIndex, HistoryListModel::TextRole).toString(),
              QStringLiteral("a"))
        << "partial text deletion updates structured data";
    ASSERT_EQ(history.data(textIndex, Qt::DisplayRole).toString(),
              QStringLiteral("a"))
        << "partial text deletion notifies standard display-role consumers";
    ASSERT_EQ(
        history.data(textIndex, HistoryListModel::EntryIdRole).toULongLong(),
        firstId)
        << "partial deletion preserves the entry ID";

    auto repeatedChord =
        chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 42, 46);
    repeatedChord.eventTimeMs = 99;
    repeatedChord.repeated = true;
    repeatedChord.repeatCount = 2;
    history.apply(repeatedChord);
    const auto chordIndex = history.index(1, 0);
    ASSERT_EQ(history.data(chordIndex, HistoryListModel::KindRole).toString(),
              QStringLiteral("chord"))
        << "chord kind is exposed to QML";
    ASSERT_EQ(history.data(chordIndex, HistoryListModel::ModifiersRole)
                  .toStringList(),
              QStringList {QStringLiteral("Ctrl")})
        << "chord modifiers remain structured";
    ASSERT_EQ(history.data(chordIndex, HistoryListModel::KeyRole).toString(),
              QStringLiteral("C"))
        << "chord key is exposed separately";
    ASSERT_EQ(history.data(chordIndex, HistoryListModel::KeycodeRole).toUInt(),
              46)
        << "key identity is available to presentation models";
    ASSERT_TRUE(
        history.data(chordIndex, HistoryListModel::EventTimeMsRole).toUInt()
            == 99
        && history.data(chordIndex, HistoryListModel::RepeatedRole).toBool()
        && history.data(chordIndex, HistoryListModel::RepeatCountRole).toUInt()
            == 2)
        << "event timing and repeat metadata are exposed without reparsing "
           "labels";

    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_TRUE(history.rowCount() == 1 && removed == 1)
        << "atomic chord deletion removes one complete model row";
    history.clear();
    ASSERT_TRUE(history.rowCount() == 0 && removed == 2)
        << "clear reports removal of the remaining model rows";

    history.apply(text(QStringLiteral("e")));
    history.apply(text(QString::fromUtf8("\xCC\x81")));
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_TRUE(history.rowCount() == 0 && removed == 3)
        << "grapheme deletion across actions removes its contiguous model rows";
}

TEST(HistoryCountedRepeats, CollapseAtTheThresholdAndFollowBackspace) {
    InputHistory history(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 4}});
    const auto first = text(QStringLiteral("a"));
    history.apply(first);
    for (std::uint32_t i = 1; i <= 4; ++i) {
        auto repeat = first;
        repeat.repeated = true;
        repeat.repeatCount = i;
        history.apply(repeat);
        const QString expected = i < 3 ? QString(i + 1, QLatin1Char('a'))
                                       : QStringLiteral("aaa…%1x ").arg(i + 1);
        ASSERT_EQ(history.presentationModel().displayText(), expected)
            << "counted repeats collapse at the configured occurrence "
               "threshold";
    }
    ASSERT_TRUE(history.rowCount() == 5
                && history.presentationModel().rowCount() == 1)
        << "projection collapses rows without discarding semantic actions";
    ASSERT_EQ(history.presentationModel().displayRichText(),
              QStringLiteral("aaa<sub><small>…5x</small></sub> "))
        << "text theme can render the total-count suffix smaller and below the "
           "baseline";
    ASSERT_EQ(history.presentationModel()
                  .data(history.presentationModel().index(0, 0),
                        HistoryProjectionModel::RepeatCountRole)
                  .toUInt(),
              5)
        << "projected count includes the initial press and generated repeats";

    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(history.presentationModel().displayText(),
              QStringLiteral("aaa…4x "))
        << "Backspace decrements the retained counted run";
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(history.presentationModel().displayText(),
              QStringLiteral("aaa…3x "))
        << "a collapsed row stays collapsed below its threshold";
    history.apply(first);
    ASSERT_EQ(history.presentationModel().displayText(),
              QStringLiteral("aaa…4x "))
        << "a new equivalent tap extends the same group after decrementing it";
    history.apply(key(QStringLiteral("Backspace")));
    history.apply(key(QStringLiteral("Backspace")));
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(history.presentationModel().displayText(), QStringLiteral("a"))
        << "a counted run returns to a plain action at one occurrence";
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_TRUE(history.presentationModel().displayText().isEmpty())
        << "Backspace removes the final underlying occurrence";
}

TEST(HistoryCountedRepeats, RetentionChargesEveryOccurrence) {
    InputHistory retained({
        .maxRetainedUtf16CodeUnits = 4,
        .presentation = {.countedRepeats = true, .repeatThreshold = 4},
    });
    auto retainedAction = text(QStringLiteral("x"));
    for (std::uint32_t i = 0; i < 5; ++i) {
        retainedAction.repeated = i > 0;
        retainedAction.repeatCount = i;
        retained.apply(retainedAction);
    }
    ASSERT_TRUE(retained.retainedUtf16CodeUnits() == 4
                && retained.displayText() == QStringLiteral("xxxx"))
        << "retention charges every underlying occurrence rather than the "
           "compressed counter";
    ASSERT_EQ(retained.presentationModel().displayText(),
              QStringLiteral("xxx…4x "))
        << "retention trimming reduces the projected occurrence count";
    retained.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(retained.presentationModel().displayText(),
              QStringLiteral("xxx…3x "))
        << "retention-trimmed groups retain sticky collapse while erasing";
}

TEST(HistoryCountedRepeats, InterleavedActionStartsANewGroup) {
    InputHistory options(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
    const auto first = text(QStringLiteral("x"));
    options.apply(first);
    options.apply(text(QStringLiteral("b"), 2, 48));
    auto later = first;
    later.repeated = true;
    later.repeatCount = 1;
    options.apply(later);
    ASSERT_EQ(options.presentationModel().displayText(), QStringLiteral("xbx"))
        << "an interleaved action starts a new counted group";

    InputHistory interleavedRuns(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
    interleavedRuns.apply(text(QStringLiteral("a"), 1, 30));
    interleavedRuns.apply(text(QStringLiteral("a"), 1, 30));
    interleavedRuns.apply(text(QStringLiteral("b"), 1, 48));
    interleavedRuns.apply(text(QStringLiteral("a"), 1, 30));
    interleavedRuns.apply(text(QStringLiteral("a"), 1, 30));
    ASSERT_EQ(interleavedRuns.presentationModel().displayText(),
              QStringLiteral("a…2x b a…2x "))
        << "matching inputs on opposite sides of another key form separate "
           "counted groups";
}

TEST(HistoryCountedRepeats, ChangedInterpretationStartsANewGroup) {
    InputHistory changed(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
    auto changedAction = text(QStringLiteral("x"));
    changed.apply(changedAction);
    changedAction.text = QStringLiteral("y");
    changedAction.repeated = true;
    changedAction.repeatCount = 1;
    changed.apply(changedAction);
    changedAction.repeatCount = 2;
    changed.apply(changedAction);
    ASSERT_EQ(changed.presentationModel().displayText(),
              QStringLiteral("x y…2x "))
        << "a changed interpretation starts a distinct counted group";
}

TEST(HistoryCountedRepeats, TapsAndGeneratedRepeatsShareAGroup) {
    InputHistory mixed(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 3}});
    auto manualFirst = text(QStringLiteral("m"), 1, 50);
    manualFirst.eventTimeMs = 1;
    mixed.apply(manualFirst);
    auto manualTap = manualFirst;
    manualTap.eventTimeMs = 600000;
    mixed.apply(manualTap);
    auto generatedRepeat = manualTap;
    generatedRepeat.repeated = true;
    generatedRepeat.repeatCount = 1;
    generatedRepeat.eventTimeMs = 600001;
    mixed.apply(generatedRepeat);
    ASSERT_EQ(mixed.presentationModel().displayText(), QStringLiteral("mm…3x "))
        << "manual taps and generated repeats share one no-timeout "
           "equivalent-input group";

    InputHistory separate(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
    auto tapA = text(QStringLiteral("a"), 1, 30);
    tapA.eventTimeMs = 1;
    auto tapB = tapA;
    tapB.eventTimeMs = 900000;
    separate.apply(tapA);
    separate.apply(tapB);
    ASSERT_EQ(separate.presentationModel().displayText(),
              QStringLiteral("a…2x "))
        << "same-key taps count together regardless of the gap between them";
}

TEST(HistoryCountedRepeats, KeyboardsCountSeparately) {
    InputHistory crossKeyboard(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
    crossKeyboard.apply(text(QStringLiteral("a"), 1, 30));
    crossKeyboard.apply(text(QStringLiteral("a"), 2, 30));
    ASSERT_EQ(crossKeyboard.presentationModel().displayText(),
              QStringLiteral("aa"))
        << "matching inputs from different keyboards remain separate";
}

TEST(HistoryCountedRepeats, ChordsAndSpecialKeysCount) {
    InputHistory chords(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
    chords.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 1, 46));
    chords.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 1, 46));
    ASSERT_EQ(chords.presentationModel().displayText(),
              QStringLiteral("Ctrl+C…2x "))
        << "identical chord taps combine across release and repress";

    InputHistory specialKeys(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
    specialKeys.apply(key(QStringLiteral("Enter"), 1, 28));
    specialKeys.apply(key(QStringLiteral("Enter"), 1, 28));
    ASSERT_EQ(specialKeys.presentationModel().displayText(),
              QStringLiteral("Enter…2x "))
        << "identical special-key taps combine across release and repress";
}

TEST(HistoryCountedRepeats, SharedSymbolsDoNotMergeDistinctKeys) {
    HistoryPresentationOptions sharedGlyphs;
    sharedGlyphs.countedRepeats = true;
    sharedGlyphs.repeatThreshold = 2;
    sharedGlyphs.keySymbols = {
        {QStringLiteral("A"), QStringLiteral("★")},
        {QStringLiteral("B"), QStringLiteral("★")},
    };
    sharedGlyphs.modifierSymbols = {
        {QStringLiteral("Ctrl"), QStringLiteral("M")},
        {QStringLiteral("Shift"), QStringLiteral("M")},
    };
    InputHistory canonicalKeys({.presentation = sharedGlyphs});
    canonicalKeys.apply(key(QStringLiteral("A"), 1, 30));
    canonicalKeys.apply(key(QStringLiteral("B"), 1, 48));
    canonicalKeys.apply(
        chord({QStringLiteral("Ctrl")}, QStringLiteral("C"), 1, 46));
    canonicalKeys.apply(
        chord({QStringLiteral("Shift")}, QStringLiteral("C"), 1, 46));
    ASSERT_EQ(canonicalKeys.presentationModel().displayText(),
              QStringLiteral("★ ★ M+C M+C "))
        << "canonical keys and modifiers remain distinct even when symbol "
           "mappings render them identically";
}

TEST(HistoryCountedRepeats, DeletionDoesNotMergeEarlierGroups) {
    InputHistory deletedIntervening(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
    deletedIntervening.apply(text(QStringLiteral("a"), 1, 30));
    deletedIntervening.apply(text(QStringLiteral("a"), 1, 30));
    deletedIntervening.apply(text(QStringLiteral("b"), 1, 48));
    deletedIntervening.apply(key(QStringLiteral("Backspace")));
    deletedIntervening.apply(text(QStringLiteral("a"), 1, 30));
    ASSERT_EQ(deletedIntervening.presentationModel().displayText(),
              QStringLiteral("a…2x a"))
        << "deleting an intervening input does not retroactively merge two "
           "previously separate groups";
}

TEST(HistoryCountedRepeats, UnsafeGraphemesStayExpanded) {
    InputHistory multiGrapheme(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
    auto twoChars = text(QStringLiteral("ab"));
    multiGrapheme.apply(twoChars);
    twoChars.repeated = true;
    twoChars.repeatCount = 1;
    multiGrapheme.apply(twoChars);
    ASSERT_EQ(multiGrapheme.presentationModel().displayText(),
              QStringLiteral("abab"))
        << "multi-grapheme actions remain expanded rather than corrupting "
           "deletion semantics";

    InputHistory joined(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 2}});
    joined.apply(text(QStringLiteral("e")));
    auto combining = text(QString::fromUtf8("\xCC\x81"));
    joined.apply(combining);
    combining.repeated = true;
    combining.repeatCount = 1;
    joined.apply(combining);
    ASSERT_EQ(joined.presentationModel().displayText(),
              QString::fromUtf8("e\xCC\x81\xCC\x81"))
        << "graphemes spanning action boundaries prevent unsafe count "
           "projection";
    joined.apply(key(QStringLiteral("Backspace")));
    ASSERT_TRUE(joined.displayText().isEmpty())
        << "Unicode deletion still removes one grapheme across source actions";
}

TEST(HistorySymbols, ChangeOnlyThePresentation) {
    HistoryPresentationOptions presentation;
    presentation.symbolFontFamily = QStringLiteral("Symbols & Friends");
    presentation.keySymbols = {
        {QStringLiteral("Backspace"), QStringLiteral("⌫")},
        {QStringLiteral("C"), QStringLiteral("COPY")},
    };
    presentation.modifierSymbols = {
        {QStringLiteral("Ctrl"), QStringLiteral("CTRL")},
    };
    InputHistory history({
        .backspaceMode = BackspaceMode::Symbol,
        .maxRetainedUtf16CodeUnits = 64,
        .presentation = presentation,
    });
    history.apply(chord({QStringLiteral("Ctrl")}, QStringLiteral("C")));
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(history.displayText(), QStringLiteral("[Ctrl+C] [Backspace] "))
        << "canonical raw labels remain available and unchanged";
    ASSERT_EQ(history.presentationModel().displayText(),
              QStringLiteral("CTRL+COPY ⌫ "))
        << "configured key and modifier labels are projected consistently";
    ASSERT_TRUE(history.entries()[0].action.key == QStringLiteral("C")
                && history.entries()[0].action.modifiers
                    == QStringList {QStringLiteral("Ctrl")})
        << "symbol substitutions do not alter canonical key or modifier "
           "identity";
    ASSERT_TRUE(history.presentationModel().displayRichText().contains(
        QStringLiteral("Symbols &amp; Friends")))
        << "rich text escapes configured symbol font names";

    InputHistory wrapped({
        .presentation = {
            .keySymbols = {
                {QStringLiteral("Enter"), QStringLiteral("[Enter]")},
            },
        },
    });
    wrapped.apply(text(QStringLiteral("a")));
    wrapped.apply(key(QStringLiteral("Enter")));
    wrapped.apply(text(QStringLiteral("b")));
    ASSERT_EQ(wrapped.presentationModel().displayText(),
              QStringLiteral("a [Enter] b"))
        << "users can opt into brackets via a key symbol";
    ASSERT_EQ(wrapped.presentationModel().displayRichText(),
              QStringLiteral("a [Enter] b"))
        << "rich text uses the same opt-in punctuation";

    const qsizetype before = history.retainedUtf16CodeUnits();
    presentation.keySymbols.insert(
        QStringLiteral("C"),
        QStringLiteral("a much longer presentation-only label"));
    history.setOptions({
        .backspaceMode = BackspaceMode::Symbol,
        .maxRetainedUtf16CodeUnits = 64,
        .presentation = presentation,
    });
    ASSERT_EQ(history.retainedUtf16CodeUnits(), before)
        << "symbol changes do not change the stable canonical retention budget";

    InputHistory deletion({.presentation = presentation});
    deletion.apply(text(QStringLiteral("abc")));
    deletion.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(deletion.displayText(), QStringLiteral("ab"))
        << "a Backspace symbol mapping cannot alter deletion identity";
}

TEST(HistorySymbols, SymbolModeBackspaceIsCountedWithItsGlyph) {
    InputHistory backspaceSymbols({
        .backspaceMode = BackspaceMode::Symbol,
        .presentation = {
            .countedRepeats = true,
            .repeatThreshold = 3,
            .keySymbols = {{QStringLiteral("Backspace"), QStringLiteral("⌫")}},
        },
    });
    auto backspace = key(QStringLiteral("Backspace"));
    backspaceSymbols.apply(backspace);
    for (std::uint32_t i = 1; i <= 2; ++i) {
        backspace.repeated = true;
        backspace.repeatCount = i;
        backspaceSymbols.apply(backspace);
    }
    ASSERT_EQ(backspaceSymbols.presentationModel().displayText(),
              QStringLiteral("⌫ ⌫…3x "))
        << "symbol-mode repeated Backspace is grouped and uses its configured "
           "display glyph";
}

TEST(HistorySymbols, SpaceSymbolIsDisplayOnly) {
    InputHistory history({
        .maxRetainedUtf16CodeUnits = 64,
        .presentation = {
            .countedRepeats = true,
            .repeatThreshold = 2,
            .spaceSymbol = QStringLiteral("␣"),
        },
    });
    history.apply(text(QStringLiteral("a b")));
    history.apply(key(QStringLiteral("Enter")));
    history.apply(text(QStringLiteral(" "), 1, 57));
    history.apply(text(QStringLiteral(" "), 1, 57));
    auto& projection = history.presentationModel();
    ASSERT_TRUE(history.displayText() == QStringLiteral("a b [Enter]   ")
                && history.retainedUtf16CodeUnits() == 14)
        << "space substitution does not change semantic text or retention "
           "accounting";
    ASSERT_EQ(projection.displayText(), QStringLiteral("a␣b Enter ␣…2x "))
        << "only input spaces change, not the separator before a key or "
           "counted "
           "suffix";
    ASSERT_TRUE(
        projection
                .data(projection.index(0, 0), HistoryProjectionModel::TextRole)
                .toString()
            == QStringLiteral("a b")
        && projection
                .data(projection.index(0, 0),
                      HistoryProjectionModel::DisplayLabelRole)
                .toString()
            == QStringLiteral("a␣b")
        && projection
                .data(projection.index(2, 0),
                      HistoryProjectionModel::DisplayLabelRole)
                .toString()
            == QStringLiteral("␣"))
        << "themes can choose canonical text or mapped display labels";
    history.apply(key(QStringLiteral("Backspace")));
    ASSERT_EQ(history.entries().back().action.text, QStringLiteral(" "))
        << "Backspace still deletes one actual space occurrence";
    const qsizetype budget = history.retainedUtf16CodeUnits();
    auto options = history.presentationOptions();
    options.spaceSymbol = QStringLiteral("<&>");
    history.setOptions(
        {.maxRetainedUtf16CodeUnits = 64, .presentation = options});
    ASSERT_TRUE(history.retainedUtf16CodeUnits() == budget
                && projection.displayRichText().contains(
                    QStringLiteral("&lt;&amp;&gt;")))
        << "live space-label updates preserve retention and HTML-escape rich "
           "text";
    ASSERT_TRUE(history.displayText().contains(QStringLiteral("a b")))
        << "semantic history remains unmodified after reload";
    HistoryListModel snapshot;
    snapshot.setSnapshot(history.entries(),
                         history.presentationOptions(),
                         history.collapsedRepeatRuns());
    ASSERT_EQ(snapshot.presentationModel().displayText(),
              projection.displayText())
        << "expiration snapshot uses the same display-only space mapping";
}

TEST(HistoryProjection, KeepsTailIdsDuringRetention) {
    InputHistory history({.maxRetainedUtf16CodeUnits = 3});
    history.apply(text(QStringLiteral("a")));
    history.apply(text(QStringLiteral("b")));
    history.apply(text(QStringLiteral("c")));
    auto& projection = history.presentationModel();
    const auto middleId =
        projection
            .data(projection.index(1, 0), HistoryProjectionModel::EntryIdRole)
            .toULongLong();
    const auto tailId =
        projection
            .data(projection.index(2, 0), HistoryProjectionModel::EntryIdRole)
            .toULongLong();
    int removedFirst = -1;
    QObject::connect(&projection,
                     &QAbstractItemModel::rowsRemoved,
                     [&removedFirst](const QModelIndex&, int first, int) {
                         removedFirst = first;
                     });
    history.apply(text(QStringLiteral("d")));
    ASSERT_TRUE(projection.rowCount() == 3
                && projection
                        .data(projection.index(0, 0),
                              HistoryProjectionModel::EntryIdRole)
                        .toULongLong()
                    == middleId
                && projection
                        .data(projection.index(1, 0),
                              HistoryProjectionModel::EntryIdRole)
                        .toULongLong()
                    == tailId)
        << "retention preserves stable IDs for surviving projected tail rows";
    ASSERT_TRUE(
        removedFirst == 0
        && projection
                .data(projection.index(2, 0), HistoryProjectionModel::TextRole)
                .toString()
            == QStringLiteral("d"))
        << "projection emits precise front-removal and tail-insertion "
           "notifications during retention";
}

TEST(HistoryProjection, NotifiesAndSnapshotsCountedState) {
    InputHistory history(
        {.presentation = {.countedRepeats = true, .repeatThreshold = 3}});
    int inserted = 0;
    int removed = 0;
    int changed = 0;
    auto& projection = history.presentationModel();
    QObject::connect(&projection,
                     &QAbstractItemModel::rowsInserted,
                     [&inserted](const QModelIndex&, int, int) { ++inserted; });
    QObject::connect(&projection,
                     &QAbstractItemModel::rowsRemoved,
                     [&removed](const QModelIndex&, int, int) { ++removed; });
    QObject::connect(&projection,
                     &QAbstractItemModel::dataChanged,
                     [&changed](const QModelIndex&,
                                const QModelIndex&,
                                const QList<int>&) { ++changed; });

    auto action = text(QStringLiteral("z"));
    history.apply(action);
    action.repeated = true;
    action.repeatCount = 1;
    history.apply(action);
    action.repeatCount = 2;
    history.apply(action);
    const auto groupId =
        projection
            .data(projection.index(0, 0), HistoryProjectionModel::EntryIdRole)
            .toULongLong();
    ASSERT_TRUE(projection.rowCount() == 1 && removed == 1 && changed == 1)
        << "crossing the threshold sends row-removal and data-change "
           "notifications";
    ASSERT_EQ(
        projection
            .data(projection.index(0, 0), HistoryProjectionModel::EntryIdRole)
            .toULongLong(),
        groupId)
        << "projected group identity stays anchored to its first retained "
           "semantic action";

    HistoryListModel snapshot;
    snapshot.setSnapshot(history.entries(),
                         history.presentationOptions(),
                         history.collapsedRepeatRuns());
    ASSERT_EQ(snapshot.presentationModel().displayText(),
              QStringLiteral("zz…3x "))
        << "expiration-style snapshots preserve counted projection state";
    ASSERT_TRUE(inserted >= 2)
        << "projection emits insert notifications for expanded semantic rows";
}

TEST(HistoryRetention, PartialTrimNotifiesDataChanged) {
    InputHistory history({.maxRetainedUtf16CodeUnits = 5});
    HistoryListModel snapshot;
    int changed = 0;
    QObject::connect(&history,
                     &QAbstractItemModel::dataChanged,
                     [&changed](const QModelIndex&,
                                const QModelIndex&,
                                const QList<int>&) { ++changed; });

    history.apply(text(QStringLiteral("abcdefg")));
    const auto retainedId =
        history.data(history.index(0, 0), HistoryListModel::EntryIdRole)
            .toULongLong();
    ASSERT_EQ(history.displayText(), QStringLiteral("cdefg"))
        << "retention truncates the structured text row at a grapheme boundary";
    ASSERT_EQ(history.data(history.index(0, 0), HistoryListModel::EntryIdRole)
                  .toULongLong(),
              retainedId)
        << "retention preserves the surviving text entry ID";
    snapshot.setSnapshot(history.entries());
    history.clear();
    ASSERT_TRUE(history.rowCount() == 0 && snapshot.rowCount() == 1)
        << "visual snapshots are independent of editable history";
    ASSERT_EQ(snapshot.displayText(), QStringLiteral("cdefg"))
        << "snapshot model uses the same rendering semantics";
    snapshot.clear();
    ASSERT_EQ(changed, 1)
        << "partial retention trimming emits dataChanged rather than a model "
           "reset";
}

TEST(HistoryRetention, RemovesWholeRowsAndKeepsSurvivingIds) {
    InputHistory multipleRows({.maxRetainedUtf16CodeUnits = 4});
    int trimRemovals = 0;
    QObject::connect(
        &multipleRows,
        &QAbstractItemModel::rowsRemoved,
        [&trimRemovals](const QModelIndex&, int, int) { ++trimRemovals; });
    multipleRows.apply(text(QStringLiteral("ab")));
    multipleRows.apply(text(QStringLiteral("cd")));
    const auto survivingId = multipleRows.entries()[1].id;
    multipleRows.apply(text(QStringLiteral("ef")));
    ASSERT_EQ(multipleRows.displayText(), QStringLiteral("cdef"))
        << "retention can remove complete old action rows";
    ASSERT_TRUE(multipleRows.rowCount() == 2
                && multipleRows.entries().front().id == survivingId
                && trimRemovals == 1)
        << "retention reports removed rows and preserves IDs of surviving "
           "actions";
}

} // namespace

int main(int argc, char** argv) {
    return tests::run<QCoreApplication>(argc, argv);
}
