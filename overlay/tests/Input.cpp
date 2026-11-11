#include "Support.hpp"
#include "history/InputHistory.hpp"
#include "history/InterpretedAction.hpp"
#include "input/KeyboardInterpreter.hpp"
#include "input/KeyboardPresenter.hpp"
#include "ipc/Protocol.hpp"

#include <gtest/gtest.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon.h>

#include <QChar>
#include <QCoreApplication>
#include <QString>
#include <QVariant>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

using namespace hyprcast::overlay;

namespace {

struct KeymapFixture {
    QString text;
    std::uint32_t shiftMask = 0;
    std::uint32_t controlMask = 0;
};

// Fills `fixture` rather than returning it: ASSERT_* needs a void function.
void compileKeymap(const char* layout, KeymapFixture& fixture) {
    const std::unique_ptr<xkb_context, decltype(&xkb_context_unref)> context {
        xkb_context_new(XKB_CONTEXT_NO_FLAGS),
        &xkb_context_unref
    };
    ASSERT_NE(context, nullptr) << "XKB context created for fixture";

    xkb_rule_names names {};
    names.rules = "evdev";
    names.model = "pc105";
    names.layout = layout;
    const std::unique_ptr<xkb_keymap, decltype(&xkb_keymap_unref)> keymap {
        xkb_keymap_new_from_names(context.get(),
                                  &names,
                                  XKB_KEYMAP_COMPILE_NO_FLAGS),
        &xkb_keymap_unref
    };
    ASSERT_NE(keymap, nullptr) << "XKB fixture keymap compiled";

    const std::unique_ptr<char, decltype(&std::free)> serialized {
        xkb_keymap_get_as_string(keymap.get(), XKB_KEYMAP_FORMAT_TEXT_V1),
        &std::free
    };
    ASSERT_NE(serialized, nullptr) << "XKB fixture keymap serialized";

    fixture.text = QString::fromUtf8(serialized.get());
    const auto shiftIndex =
        xkb_keymap_mod_get_index(keymap.get(), XKB_MOD_NAME_SHIFT);
    const auto ctrlIndex =
        xkb_keymap_mod_get_index(keymap.get(), XKB_MOD_NAME_CTRL);
    ASSERT_TRUE(shiftIndex != XKB_MOD_INVALID && ctrlIndex != XKB_MOD_INVALID)
        << "XKB fixture has Shift and Control modifiers";
    fixture.shiftMask =
        static_cast<std::uint32_t>(xkb_mod_mask_t {1} << shiftIndex);
    fixture.controlMask =
        static_cast<std::uint32_t>(xkb_mod_mask_t {1} << ctrlIndex);
}

KeymapFixture makeKeymap(const char* layout) {
    KeymapFixture fixture;
    compileKeymap(layout, fixture);
    return fixture;
}

using TimePoint = KeyboardInterpreter::Clock::time_point;

QStringList heldLabels(const KeyboardInterpreter& interpreter) {
    QStringList labels;
    for (const HeldKey& item : interpreter.heldKeyItems()) {
        labels.push_back(item.label());
    }
    return labels;
}

void send(KeyboardInterpreter& interpreter,
          const ProtocolMessage& message,
          TimePoint now) {
    ASSERT_TRUE(interpreter.process(message, now).has_value())
        << "valid keyboard message interpreted without error";
}

KeyboardSnapshotMessage snapshot(const KeymapFixture& keymap,
                                 std::uint32_t id = 1,
                                 std::uint32_t rate = 0,
                                 std::uint32_t delay = 0) {
    return {
        .id = id,
        .name = QStringLiteral("test keyboard"),
        .keymap = keymap.text,
        .repeatInfo = {.rate = rate, .delay = delay},
    };
}

KeyMessage key(std::uint32_t id,
               std::uint32_t code,
               bool pressed,
               std::uint32_t timeMs = 1) {
    return {
        .keyboardId = id,
        .timeMs = timeMs,
        .keycode = code,
        .pressed = pressed,
    };
}

std::vector<InterpretedAction> process(KeyboardInterpreter& interpreter,
                                       const ProtocolMessage& message,
                                       TimePoint now) {
    auto actions = interpreter.process(message, now);
    if (!actions) {
        ADD_FAILURE() << "keyboard message rejected: "
                      << actions.error().toStdString();
        return {};
    }
    return std::move(*actions);
}

TEST(KeyboardInterpreterTest, TextUsesTheXkbKeycodeOffset) {
    const auto keymap = makeKeymap("us");
    KeyboardInterpreter interpreter;
    const auto now = TimePoint {};
    send(interpreter, snapshot(keymap), now);

    auto actions = process(interpreter, key(1, 30, true), now);
    ASSERT_EQ(actions.size(), 1)
        << "one action emitted for an ordinary key press";
    ASSERT_TRUE(actions.front().kind == InterpretedActionKind::Text
                && actions.front().text == QStringLiteral("a"))
        << "evdev keycode 30 maps to XKB keycode 38 and produces a";
    ASSERT_TRUE(actions.front().keyboardId == 1
                && actions.front().keycode == 30)
        << "action retains keyboard and source key identity";
    ASSERT_TRUE(process(interpreter, key(1, 30, false, 2), now).empty())
        << "ordinary key release does not append output";
}

TEST(KeyboardInterpreterTest, ShiftIsTextChordOrStandaloneKey) {
    const auto keymap = makeKeymap("us");
    KeyboardInterpreter interpreter;
    const auto now = TimePoint {};
    send(interpreter, snapshot(keymap), now);

    ASSERT_TRUE(process(interpreter, key(1, 42, true), now).empty())
        << "modifier press is held rather than appended";
    ASSERT_EQ(heldLabels(interpreter), QStringList {QStringLiteral("Shift")})
        << "held modifier is exposed immediately";
    send(interpreter,
         ModifiersMessage {
             .keyboardId = 1,
             .modifiers = {.depressed = keymap.shiftMask},
         },
         now);
    const auto shifted = process(interpreter, key(1, 30, true, 2), now);
    ASSERT_TRUE(shifted.size() == 1
                && shifted.front().kind == InterpretedActionKind::Text
                && shifted.front().text == QStringLiteral("A"))
        << "Shift+A produces text A instead of a modifier chord";
    ASSERT_EQ(shifted.front().modifiers, QStringList {QStringLiteral("Shift")})
        << "text action retains the modifier identity used for translation";
    ASSERT_TRUE(process(interpreter, key(1, 30, false, 3), now).empty())
        << "shifted letter release emits nothing";
    ASSERT_TRUE(process(interpreter, key(1, 42, false, 4), now).empty())
        << "modifier used for text is not emitted as a standalone key";
    send(interpreter, ModifiersMessage {.keyboardId = 1}, now);
    ASSERT_TRUE(heldLabels(interpreter).isEmpty())
        << "released modifier is no longer held";

    process(interpreter, key(1, 42, true, 5), now);
    send(interpreter,
         ModifiersMessage {
             .keyboardId = 1,
             .modifiers = {.depressed = keymap.shiftMask},
         },
         now);
    const auto shiftedNavigation =
        process(interpreter, key(1, 102, true, 6), now);
    ASSERT_TRUE(shiftedNavigation.size() == 1
                && shiftedNavigation.front().kind
                    == InterpretedActionKind::Chord
                && shiftedNavigation.front().modifiers
                    == QStringList {QStringLiteral("Shift")}
                && shiftedNavigation.front().key == QStringLiteral("Home"))
        << "unconsumed Shift on a non-text key is preserved as a chord";
    process(interpreter, key(1, 102, false, 7), now);
    process(interpreter, key(1, 42, false, 8), now);
    send(interpreter, ModifiersMessage {.keyboardId = 1}, now);

    ASSERT_TRUE(process(interpreter, key(1, 29, true, 9), now).empty())
        << "standalone Control press is held";
    ASSERT_EQ(heldLabels(interpreter), QStringList {QStringLiteral("Ctrl")})
        << "standalone Control is shown while held";
    const auto standalone = process(interpreter, key(1, 29, false, 6), now);
    ASSERT_TRUE(standalone.size() == 1
                && standalone.front().kind == InterpretedActionKind::Key
                && standalone.front().key == QStringLiteral("Ctrl"))
        << "unused standalone modifier is emitted on release";
}

TEST(KeyboardInterpreterTest, ChordsUseTheModifiersOfTheirKeyboard) {
    const auto keymap = makeKeymap("us");
    KeyboardInterpreter interpreter;
    const auto now = TimePoint {};
    send(interpreter, snapshot(keymap, 1), now);
    send(interpreter, snapshot(keymap, 2), now);

    ASSERT_TRUE(process(interpreter, key(1, 29, true), now).empty())
        << "Control held on first keyboard";
    send(interpreter,
         ModifiersMessage {
             .keyboardId = 1,
             .modifiers = {.depressed = keymap.controlMask},
         },
         now);
    const auto otherKeyboard = process(interpreter, key(2, 46, true), now);
    ASSERT_TRUE(otherKeyboard.size() == 1
                && otherKeyboard.front().kind == InterpretedActionKind::Text
                && otherKeyboard.front().text == QStringLiteral("c"))
        << "modifier state on one keyboard does not leak to another";

    const auto chord = process(interpreter, key(1, 46, true, 2), now);
    ASSERT_TRUE(chord.size() == 1
                && chord.front().kind == InterpretedActionKind::Chord)
        << "Control+C is interpreted as a chord";
    ASSERT_TRUE(chord.front().key == QStringLiteral("C")
                && chord.front().modifiers
                    == QStringList {QStringLiteral("Ctrl")})
        << "chord retains its key and modifier identities";
    ASSERT_TRUE(process(interpreter, key(1, 46, false, 3), now).empty())
        << "chord key release emits no second action";
    ASSERT_TRUE(process(interpreter, key(1, 29, false, 4), now).empty())
        << "modifier used in a chord is not emitted standalone";
}

TEST(KeyboardInterpreterTest, ConsumedAltGrYieldsTheLayoutCharacter) {
    const auto keymap = makeKeymap("de");
    KeyboardInterpreter interpreter;
    const auto now = TimePoint {};
    send(interpreter, snapshot(keymap), now);

    ASSERT_TRUE(process(interpreter, key(1, 100, true), now).empty())
        << "AltGr press is held";
    const auto atSign = process(interpreter, key(1, 16, true, 2), now);
    ASSERT_TRUE(atSign.size() == 1
                && atSign.front().kind == InterpretedActionKind::Text
                && atSign.front().text == QStringLiteral("@"))
        << "consumed AltGr modifier yields the layout character, not an AltGr "
           "chord";
    ASSERT_EQ(atSign.front().modifiers, QStringList {QStringLiteral("AltGr")})
        << "layout-produced text retains its consumed AltGr identity";
}

TEST(KeyboardInterpreterTest, KeymapChangeAndRemovalResetInputState) {
    const auto keymap = makeKeymap("us");
    KeyboardInterpreter interpreter;
    const auto now = TimePoint {};
    send(interpreter, snapshot(keymap, 1, 20, 200), now);
    process(interpreter, key(1, 42, true), now);
    process(interpreter, key(1, 30, true), now);
    ASSERT_TRUE(heldLabels(interpreter).contains(QStringLiteral("Shift")))
        << "held modifier recorded before keymap replacement";
    ASSERT_EQ(heldLabels(interpreter).size(), 2)
        << "held-key snapshot includes the modifier and ordinary key";
    ASSERT_NE(interpreter.nextRepeatDeadline(), TimePoint::max())
        << "repeat active before keymap replacement";

    send(interpreter,
         KeymapMessage {.keyboardId = 1, .keymap = keymap.text},
         now + std::chrono::milliseconds(50));
    ASSERT_TRUE(heldLabels(interpreter).isEmpty())
        << "keymap replacement clears observed held keys";
    ASSERT_EQ(interpreter.nextRepeatDeadline(), TimePoint::max())
        << "keymap replacement cancels repeats";

    process(interpreter,
            key(1, 30, true, 3),
            now + std::chrono::milliseconds(60));
    ASSERT_TRUE(interpreter.nextRepeatDeadline() != TimePoint::max()
                && heldLabels(interpreter) == QStringList {QStringLiteral("a")})
        << "new key presses use the replacement keymap";
    send(interpreter,
         UnsubscribeKeyboardMessage {.id = 1},
         now + std::chrono::milliseconds(70));
    ASSERT_TRUE(interpreter.nextRepeatDeadline() == TimePoint::max()
                && heldLabels(interpreter).isEmpty())
        << "keyboard removal clears held keys and cancels repeats";
    ASSERT_TRUE(process(interpreter,
                        key(1, 30, true, 4),
                        now + std::chrono::milliseconds(80))
                    .empty())
        << "removed keyboard cannot produce further output";
}

// The plugin withdraws the keyboards when input stops flowing (capture paused,
// session locked) and announces them again, as they are then, when it resumes.
// Nothing from before the gap may survive it.
TEST(KeyboardInterpreterTest, ResentSnapshotReplacesKeyboardState) {
    const auto keymap = makeKeymap("us");
    KeyboardInterpreter interpreter;
    const auto start = TimePoint {};
    const auto holdShiftAndA = [&](TimePoint now) {
        process(interpreter, key(1, 42, true), now);
        send(interpreter,
             ModifiersMessage {
                 .keyboardId = 1,
                 .modifiers = {.depressed = keymap.shiftMask},
             },
             now);
        const auto shifted = process(interpreter, key(1, 30, true, 2), now);
        ASSERT_TRUE(shifted.size() == 1
                    && shifted.front().text == QStringLiteral("A")
                    && heldLabels(interpreter).size() == 2
                    && interpreter.nextRepeatDeadline() != TimePoint::max())
            << "Shift+A is held and repeating before the gap";
    };
    const auto expectPlainA = [&](TimePoint now, const char* message) {
        const auto plain = process(interpreter, key(1, 30, true, 3), now);
        ASSERT_TRUE(
            plain.size() == 1 && plain.front().text == QStringLiteral("a")
            && plain.front().modifiers.isEmpty()
            && heldLabels(interpreter) == QStringList {QStringLiteral("a")})
            << message;
        process(interpreter, key(1, 30, false, 4), now);
    };

    // Paused and enabled again: the state first, then the keyboards.
    send(interpreter, snapshot(keymap, 1, 20, 200), start);
    holdShiftAndA(start);
    auto now = start + std::chrono::milliseconds(50);
    send(interpreter, CastingStateMessage {.paused = true}, now);
    send(interpreter, UnsubscribeKeyboardMessage {.id = 1}, now);
    ASSERT_TRUE(heldLabels(interpreter).isEmpty()
                && interpreter.nextRepeatDeadline() == TimePoint::max()
                && interpreter.advance(start + std::chrono::seconds(5)).empty())
        << "nothing stays held or repeats once input stops";
    now = start + std::chrono::seconds(6);
    send(interpreter, CastingStateMessage {.paused = false}, now);
    send(interpreter, snapshot(keymap, 1, 20, 200), now);
    ASSERT_TRUE(heldLabels(interpreter).isEmpty()
                && interpreter.nextRepeatDeadline() == TimePoint::max())
        << "a keyboard announced again starts with nothing held";
    expectPlainA(now, "Shift held before a pause does not shift text after it");

    // Locked and unlocked while capture stays enabled: only the keyboards go
    // and come back.
    holdShiftAndA(now);
    now += std::chrono::milliseconds(50);
    send(interpreter, UnsubscribeKeyboardMessage {.id = 1}, now);
    ASSERT_TRUE(heldLabels(interpreter).isEmpty()
                && interpreter.nextRepeatDeadline() == TimePoint::max())
        << "nothing stays held or repeats while locked";
    now += std::chrono::seconds(6);
    send(interpreter, snapshot(keymap, 1, 20, 200), now);
    expectPlainA(now, "Shift held before a lock does not shift text after it");

    // A keyboard announced while it is already known is replaced as a whole,
    // and what the snapshot says about its modifiers is what counts.
    holdShiftAndA(now);
    now += std::chrono::milliseconds(50);
    send(interpreter, snapshot(keymap, 1, 20, 200), now);
    ASSERT_TRUE(heldLabels(interpreter).isEmpty()
                && interpreter.nextRepeatDeadline() == TimePoint::max())
        << "a repeated announcement drops held keys and repeats";
    expectPlainA(now, "a repeated announcement drops stale modifiers");

    auto shiftDown = snapshot(keymap, 1, 20, 200);
    shiftDown.modifiers.depressed = keymap.shiftMask;
    send(interpreter, shiftDown, now);
    const auto shifted = process(interpreter, key(1, 30, true, 5), now);
    ASSERT_TRUE(shifted.size() == 1
                && shifted.front().text == QStringLiteral("A"))
        << "modifiers down at the moment of the snapshot are applied";
}

TEST(KeyboardPresenterTest, HistorySurvivesPauseAndReset) {
    const auto keymap = makeKeymap("us");
    KeyboardPresenter presenter;
    presenter.processMessage(snapshot(keymap));
    presenter.processMessage(key(1, 30, true));
    ASSERT_EQ(presenter.outputText(), QStringLiteral("a"))
        << "presenter records interpreted text";
    ASSERT_TRUE(presenter.heldKeys() == QStringList {QStringLiteral("a")}
                && presenter.heldKeyCount() == 1)
        << "presenter exposes held non-modifier keys and a QML-friendly count";

    presenter.resetConnection();
    ASSERT_EQ(presenter.outputText(), QStringLiteral("a"))
        << "connection reset preserves retained history";
    ASSERT_TRUE(presenter.heldKeys().isEmpty())
        << "connection reset clears the live held-key presentation";
    presenter.processMessage(snapshot(keymap));
    presenter.processMessage(key(1, 48, true));
    ASSERT_EQ(presenter.outputText(), QStringLiteral("ab"))
        << "input resumes on a fresh keyboard snapshot without clearing "
           "retained history";

    presenter.processMessage(CastingStateMessage {.paused = true});
    ASSERT_EQ(presenter.outputText(), QStringLiteral("ab"))
        << "pausing input preserves retained history";
    ASSERT_TRUE(presenter.heldKeys().isEmpty() && presenter.heldKeyCount() == 0)
        << "pausing clears the live held-key presentation";
    presenter.resetConnection();
    ASSERT_EQ(presenter.outputText(), QStringLiteral("ab"))
        << "reset after pause still preserves retained history";
}

TEST(KeyboardPresenterTest, ExplicitClearRemovesHistoryAndFade) {
    const auto keymap = makeKeymap("us");
    const auto start = TimePoint {};
    KeyboardPresenter presenter;
    presenter.setExpiration(1000, 250, start);
    presenter.processMessage(snapshot(keymap), start);
    presenter.processMessage(key(1, 30, true), start);
    presenter.processMessage(key(1, 30, false), start);
    presenter.clearHistory();
    ASSERT_TRUE(presenter.outputText().isEmpty())
        << "explicit clear removes active history";
    presenter.processMessage(key(1, 48, true), start);
    presenter.processMessage(key(1, 48, false), start);
    presenter.advance(start + std::chrono::milliseconds(1000));
    ASSERT_TRUE(presenter.fading()) << "history has a fade snapshot";
    presenter.clearHistory();
    ASSERT_TRUE(!presenter.fading()
                && presenter.fadingHistoryModel().rowCount() == 0)
        << "explicit clear removes fade snapshot";
    presenter.advance(start + std::chrono::milliseconds(5000));
    ASSERT_TRUE(presenter.outputText().isEmpty() && !presenter.fading())
        << "cleared history cannot reappear";
}

TEST(HistoryExpiration, ExpiredHistoryFadesAsASnapshot) {
    const auto keymap = makeKeymap("us");
    const auto start = TimePoint {};
    KeyboardPresenter presenter;
    presenter.setExpiration(1000, 250, start);
    presenter.processMessage(snapshot(keymap), start);
    presenter.processMessage(key(1, 30, true),
                             start + std::chrono::milliseconds(1));
    presenter.processMessage(key(1, 30, false),
                             start + std::chrono::milliseconds(2));
    ASSERT_EQ(presenter.outputText(), QStringLiteral("a"))
        << "history remains active before its idle deadline";

    presenter.advance(start + std::chrono::milliseconds(1000));
    ASSERT_TRUE(!presenter.fading() && presenter.historyModel().rowCount() == 1)
        << "history does not expire before the idle deadline";
    presenter.advance(start + std::chrono::milliseconds(1001));
    ASSERT_TRUE(presenter.fading() && presenter.fadeDurationMs() == 250)
        << "expired history enters the configured optional visual fade";
    ASSERT_EQ(presenter.historyModel().rowCount(), 0)
        << "expired content is immediately removed from editable history";
    ASSERT_EQ(presenter.fadingHistoryModel().displayText(), QStringLiteral("a"))
        << "fading uses a separate presentation-only snapshot";

    presenter.processMessage(key(1, 14, true),
                             start + std::chrono::milliseconds(1100));
    ASSERT_EQ(presenter.historyModel().rowCount(), 0)
        << "Backspace cannot consume entries already expired from editable "
           "history";
    presenter.processMessage(key(1, 14, false),
                             start + std::chrono::milliseconds(1101));
    presenter.advance(start + std::chrono::milliseconds(1251));
    ASSERT_TRUE(!presenter.fading()
                && presenter.fadingHistoryModel().rowCount() == 0)
        << "fade completion clears only the visual snapshot";

    presenter.processMessage(key(1, 48, true),
                             start + std::chrono::milliseconds(1300));
    ASSERT_TRUE(presenter.outputText() == QStringLiteral("b")
                && !presenter.fading())
        << "new input starts a fresh editable history after expiration";
}

TEST(HistoryExpiration, HeldModifierOutlivesTheFade) {
    const auto keymap = makeKeymap("us");
    const auto start = TimePoint {};
    KeyboardPresenter heldDuringFade;
    heldDuringFade.setExpiration(1000, 250, start);
    heldDuringFade.processMessage(snapshot(keymap), start);
    heldDuringFade.processMessage(key(1, 30, true),
                                  start + std::chrono::milliseconds(1));
    heldDuringFade.processMessage(key(1, 30, false),
                                  start + std::chrono::milliseconds(2));
    heldDuringFade.processMessage(key(1, 42, true),
                                  start + std::chrono::milliseconds(3));
    heldDuringFade.advance(start + std::chrono::milliseconds(1001));
    ASSERT_TRUE(heldDuringFade.fading()
                && heldDuringFade.heldKeys()
                    == QStringList {QStringLiteral("Shift")})
        << "held modifier state remains live when history enters its fade";
    heldDuringFade.advance(start + std::chrono::milliseconds(1251));
    ASSERT_TRUE(!heldDuringFade.fading()
                && heldDuringFade.heldKeys()
                    == QStringList {QStringLiteral("Shift")})
        << "finishing the history fade does not clear a still-held modifier";
}

TEST(HistoryExpiration, NewInputCancelsTheFade) {
    const auto keymap = makeKeymap("us");
    const auto start = TimePoint {};
    KeyboardPresenter recovery;
    recovery.setExpiration(1000, 250, start);
    recovery.processMessage(snapshot(keymap), start);
    recovery.processMessage(key(1, 30, true),
                            start + std::chrono::milliseconds(1));
    recovery.advance(start + std::chrono::milliseconds(1001));
    recovery.processMessage(key(1, 48, true),
                            start + std::chrono::milliseconds(1100));
    ASSERT_TRUE(recovery.outputText() == QStringLiteral("b")
                && !recovery.fading()
                && recovery.fadingHistoryModel().rowCount() == 0)
        << "new history cancels the old visual snapshot and reverses an "
           "in-progress fade";
    recovery.advance(start + std::chrono::milliseconds(1251));
    ASSERT_EQ(recovery.outputText(), QStringLiteral("b"))
        << "the cancelled fade timer cannot expire newer input";
}

TEST(HistoryExpiration, ZeroFadeExpiresAtOnce) {
    const auto keymap = makeKeymap("us");
    const auto start = TimePoint {};
    KeyboardPresenter immediate;
    immediate.setExpiration(500, 0, start);
    immediate.processMessage(snapshot(keymap), start);
    immediate.processMessage(key(1, 30, true),
                             start + std::chrono::milliseconds(1));
    immediate.advance(start + std::chrono::milliseconds(501));
    ASSERT_TRUE(immediate.historyModel().rowCount() == 0 && !immediate.fading())
        << "zero fade duration expires history immediately";
}

TEST(HistoryExpiration, NewInactivityRestartsTheDeadline) {
    const auto keymap = makeKeymap("us");
    const auto start = TimePoint {};
    KeyboardPresenter expirationReload;
    expirationReload.setExpiration(1000, 0, start);
    expirationReload.processMessage(snapshot(keymap), start);
    expirationReload.processMessage(key(1, 30, true),
                                    start + std::chrono::milliseconds(1));
    expirationReload.setExpiration(2000,
                                   0,
                                   start + std::chrono::milliseconds(500));
    expirationReload.advance(start + std::chrono::milliseconds(1499));
    ASSERT_EQ(expirationReload.historyModel().rowCount(), 1)
        << "live inactivity changes restart the current history deadline";
    expirationReload.advance(start + std::chrono::milliseconds(2500));
    ASSERT_EQ(expirationReload.historyModel().rowCount(), 0)
        << "history expires using the newly accepted inactivity interval";
}

TEST(HistoryExpiration, FadeDurationAloneKeepsTheDeadline) {
    const auto keymap = makeKeymap("us");
    const auto start = TimePoint {};
    KeyboardPresenter fadeOnlyReload;
    fadeOnlyReload.setExpiration(1000, 0, start);
    fadeOnlyReload.processMessage(snapshot(keymap), start);
    fadeOnlyReload.processMessage(key(1, 30, true),
                                  start + std::chrono::milliseconds(1));
    fadeOnlyReload.setExpiration(1000,
                                 250,
                                 start + std::chrono::milliseconds(500));
    fadeOnlyReload.advance(start + std::chrono::milliseconds(1001));
    ASSERT_TRUE(fadeOnlyReload.fading())
        << "changing fade duration does not reset the active inactivity "
           "deadline";
}

TEST(HistoryExpiration, ReconfiguredFadeRestarts) {
    const auto keymap = makeKeymap("us");
    const auto start = TimePoint {};
    KeyboardPresenter reconfiguredFade;
    reconfiguredFade.setExpiration(1000, 250, start);
    reconfiguredFade.processMessage(snapshot(keymap), start);
    reconfiguredFade.processMessage(key(1, 30, true),
                                    start + std::chrono::milliseconds(1));
    reconfiguredFade.advance(start + std::chrono::milliseconds(1001));
    reconfiguredFade.setExpiration(0,
                                   500,
                                   start + std::chrono::milliseconds(1100));
    ASSERT_TRUE(reconfiguredFade.fading()
                && reconfiguredFade.fadeDurationMs() == 500)
        << "live fade-duration changes restart the active visual fade";
    reconfiguredFade.advance(start + std::chrono::milliseconds(1251));
    ASSERT_TRUE(reconfiguredFade.fading())
        << "the old fade deadline cannot clear a reconfigured snapshot";
    reconfiguredFade.advance(start + std::chrono::milliseconds(1599));
    ASSERT_TRUE(reconfiguredFade.fading())
        << "reconfigured fade remains visible until its new deadline";
    reconfiguredFade.advance(start + std::chrono::milliseconds(1600));
    ASSERT_FALSE(reconfiguredFade.fading())
        << "reconfigured fade clears at its new deadline even when expiration "
           "is disabled";
}

TEST(KeyboardPresenterTest, CountedProjectionSurvivesBackspaceAndReconnect) {
    const auto start = TimePoint {};
    const auto keymap = makeKeymap("us");
    KeyboardPresenter presenter;
    InputHistoryOptions options;
    options.presentation.countedRepeats = true;
    options.presentation.repeatThreshold = 3;
    options.presentation.modifierSymbols = {
        {QStringLiteral("Ctrl"), QStringLiteral("⌃")},
    };
    presenter.setHistoryOptions(options);
    presenter.processMessage(snapshot(keymap, 1, 10, 100), start);
    presenter.processMessage(key(1, 30, true), start);
    presenter.advance(start + std::chrono::milliseconds(100));
    presenter.advance(start + std::chrono::milliseconds(200));
    ASSERT_EQ(presenter.historyModel().presentationModel().displayText(),
              QStringLiteral("aa…3x "))
        << "presenter projects one initial press plus generated repeats as a "
           "counted group";

    presenter.processMessage(key(1, 14, true, 201),
                             start + std::chrono::milliseconds(201));
    ASSERT_EQ(presenter.historyModel().presentationModel().displayText(),
              QStringLiteral("aa…2x "))
        << "plain Backspace deletes one underlying occurrence of a counted "
           "group";
    presenter.resetConnection();
    ASSERT_EQ(presenter.historyModel().presentationModel().displayText(),
              QStringLiteral("aa…2x "))
        << "pause/reconnect input reset preserves counted semantic history and "
           "sticky presentation";

    presenter.processMessage(snapshot(keymap, 1),
                             start + std::chrono::milliseconds(300));
    presenter.processMessage(key(1, 30, true, 301),
                             start + std::chrono::milliseconds(301));
    ASSERT_EQ(presenter.historyModel().presentationModel().displayText(),
              QStringLiteral("aa…3x "))
        << "a post-reconnect equivalent input joins the adjacent retained "
           "sequence";

    presenter.processMessage(key(1, 29, true, 302),
                             start + std::chrono::milliseconds(302));
    const auto held = presenter.heldKeyItems();
    bool mappedCtrl = false;
    for (const QVariant& value : held) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("identity")).toString()
            == QStringLiteral("Ctrl")) {
            mappedCtrl = item.value(QStringLiteral("label")).toString()
                == QStringLiteral("⌃");
        }
    }
    ASSERT_TRUE(mappedCtrl)
        << "held-key feedback resolves configured modifier labels while "
           "retaining raw identity";

    KeyboardPresenter expiration;
    expiration.setHistoryOptions(options);
    expiration.setExpiration(100, 250, start);
    expiration.processMessage(snapshot(keymap, 4, 10, 100), start);
    expiration.processMessage(key(4, 30, true), start);
    expiration.advance(start + std::chrono::milliseconds(100));
    expiration.advance(start + std::chrono::milliseconds(200));
    expiration.processMessage(key(4, 30, false, 201),
                              start + std::chrono::milliseconds(201));
    expiration.advance(start + std::chrono::milliseconds(301));
    ASSERT_TRUE(
        expiration.fadingHistoryModel().presentationModel().displayText()
            == QStringLiteral("aa…3x ")
        && expiration.historyModel().rowCount() == 0)
        << "expiration snapshots retain counted projection while immediately "
           "clearing editable history";
}

TEST(KeyboardPresenterTest, ManualTapsCountWithAutoRepeatDisabled) {
    const auto keymap = makeKeymap("us");
    const auto start = TimePoint {};
    KeyboardPresenter presenter;
    InputHistoryOptions options;
    options.presentation.countedRepeats = true;
    options.presentation.repeatThreshold = 4;
    presenter.setHistoryOptions(options);
    presenter.setRepeatsEnabled(false, start);
    presenter.processMessage(snapshot(keymap, 1, 20, 100), start);

    for (int i = 0; i < 4; ++i) {
        const auto now = start + std::chrono::milliseconds(i * 200);
        presenter.processMessage(
            key(1, 30, true, static_cast<std::uint32_t>((i * 200) + 1)),
            now);
        presenter.processMessage(
            key(1, 30, false, static_cast<std::uint32_t>((i * 200) + 2)),
            now + std::chrono::milliseconds(2));
        presenter.advance(now + std::chrono::milliseconds(150));
        const QString expected = i < 3 ? QString(i + 1, QLatin1Char('a'))
                                       : QStringLiteral("aaa…4x ");
        ASSERT_EQ(presenter.historyModel().presentationModel().displayText(),
                  expected)
            << "separate same-key taps count with auto-repeat disabled";
    }

    presenter.processMessage(key(1, 14, true, 801),
                             start + std::chrono::milliseconds(800));
    ASSERT_EQ(presenter.historyModel().presentationModel().displayText(),
              QStringLiteral("aaa…3x "))
        << "Backspace decrements a manually counted group";
    presenter.processMessage(key(1, 14, false, 802),
                             start + std::chrono::milliseconds(802));
    presenter.processMessage(key(1, 30, true, 803),
                             start + std::chrono::milliseconds(803));
    ASSERT_EQ(presenter.historyModel().presentationModel().displayText(),
              QStringLiteral("aaa…4x "))
        << "a later equivalent tap joins the decremented manual group";
}

TEST(KeyboardInterpreterTest, RepeatsFollowTheConfiguredTiming) {
    const auto keymap = makeKeymap("us");
    KeyboardInterpreter interpreter;
    const auto start = TimePoint {};
    send(interpreter, snapshot(keymap, 1, 10, 500), start);

    const auto initial = process(interpreter, key(1, 30, true), start);
    ASSERT_TRUE(initial.size() == 1 && !initial.front().repeated
                && initial.front().text == QStringLiteral("a"))
        << "initial key press is interpreted once and is not marked as repeat";
    ASSERT_TRUE(
        interpreter.advance(start + std::chrono::milliseconds(499)).empty())
        << "repeat does not fire before configured delay";
    auto repeated = interpreter.advance(start + std::chrono::milliseconds(500));
    ASSERT_TRUE(repeated.size() == 1 && repeated.front().repeated
                && repeated.front().repeatCount == 1
                && repeated.front().text == initial.front().text)
        << "first local repeat preserves the original interpretation and "
           "increments repeat metadata";
    repeated = interpreter.advance(start + std::chrono::milliseconds(600));
    ASSERT_TRUE(repeated.size() == 1 && repeated.front().repeatCount == 2)
        << "repeat interval follows configured rate";

    send(interpreter,
         RepeatInfoMessage {
             .keyboardId = 1,
             .repeatInfo = {.rate = 20, .delay = 300},
         },
         start + std::chrono::milliseconds(650));
    ASSERT_TRUE(
        interpreter.advance(start + std::chrono::milliseconds(949)).empty())
        << "repeat setting changes restart the delay for held keys";
    repeated = interpreter.advance(start + std::chrono::milliseconds(950));
    ASSERT_TRUE(repeated.size() == 1 && repeated.front().repeatCount == 3)
        << "updated repeat settings are applied to held keys";

    ASSERT_TRUE(process(interpreter,
                        key(1, 30, false, 3),
                        start + std::chrono::milliseconds(951))
                    .empty())
        << "release cancels repeat without output";
    ASSERT_EQ(interpreter.nextRepeatDeadline(), TimePoint::max())
        << "no repeat remains scheduled after release";
    ASSERT_TRUE(interpreter.advance(start + std::chrono::seconds(10)).empty())
        << "released key never repeats later";

    const auto repress = process(interpreter,
                                 key(1, 30, true, 4),
                                 start + std::chrono::seconds(11));
    ASSERT_TRUE(repress.size() == 1 && !repress.front().repeated
                && repress.front().repeatCount == 0
                && repress.front().text == initial.front().text)
        << "release and repress produce a new ordinary input while repeat "
           "scheduling restarts";
    send(interpreter,
         CastingStateMessage {.paused = true},
         start + std::chrono::seconds(11));
    ASSERT_TRUE(heldLabels(interpreter).isEmpty())
        << "pause clears observed held state";
    ASSERT_EQ(interpreter.nextRepeatDeadline(), TimePoint::max())
        << "pause cancels active repeats";
    ASSERT_TRUE(interpreter.advance(start + std::chrono::seconds(20)).empty())
        << "paused key does not repeat";
}

TEST(KeyboardInterpreterTest, DisablingRepeatsCancelsHeldDeadlines) {
    const auto keymap = makeKeymap("us");
    const auto start = TimePoint {};
    KeyboardInterpreter toggle;
    send(toggle, snapshot(keymap, 2, 10, 500), start);
    process(toggle, key(2, 30, true), start);
    toggle.setRepeatsEnabled(false, start + std::chrono::milliseconds(100));
    ASSERT_EQ(toggle.nextRepeatDeadline(), TimePoint::max())
        << "disabling repeat removes existing held-key deadlines";
    ASSERT_TRUE(toggle.advance(start + std::chrono::seconds(2)).empty())
        << "a disabled repeat cannot fire later";
    toggle.setRepeatsEnabled(true, start + std::chrono::seconds(2));
    ASSERT_TRUE(toggle.advance(start + std::chrono::milliseconds(2499)).empty())
        << "enabling repeat restarts the configured initial delay";
    const auto repeated =
        toggle.advance(start + std::chrono::milliseconds(2500));
    ASSERT_TRUE(repeated.size() == 1 && repeated.front().repeated)
        << "repeat resumes for a key that remains held after re-enabling";
}

} // namespace

int main(int argc, char** argv) {
    return tests::run<QCoreApplication>(argc, argv);
}
