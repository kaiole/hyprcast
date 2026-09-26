#include "input/KeyboardInterpreter.hpp"
#include "input/KeyboardPresenter.hpp"

#include <xkbcommon/xkbcommon.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace Hyprcast::Overlay;

namespace {
    void check(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "check failed: " << message << '\n';
            std::exit(1);
        }
    }

    struct KeymapFixture {
        QString       text;
        std::uint32_t shiftMask   = 0;
        std::uint32_t controlMask = 0;
    };

    KeymapFixture makeKeymap(const char* layout) {
        xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        check(context != nullptr, "XKB context created for fixture");

        xkb_rule_names names{};
        names.rules        = "evdev";
        names.model        = "pc105";
        names.layout       = layout;
        xkb_keymap* keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        check(keymap != nullptr, "XKB fixture keymap compiled");

        std::unique_ptr<char, decltype(&std::free)> serialized{xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1), &std::free};
        check(serialized != nullptr, "XKB fixture keymap serialized");

        KeymapFixture fixture;
        fixture.text          = QString::fromUtf8(serialized.get());
        const auto shiftIndex = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
        const auto ctrlIndex  = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
        check(shiftIndex != XKB_MOD_INVALID && ctrlIndex != XKB_MOD_INVALID, "XKB fixture has Shift and Control modifiers");
        fixture.shiftMask   = static_cast<std::uint32_t>(xkb_mod_mask_t{1} << shiftIndex);
        fixture.controlMask = static_cast<std::uint32_t>(xkb_mod_mask_t{1} << ctrlIndex);

        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        return fixture;
    }

    using TimePoint = KeyboardInterpreter::Clock::time_point;

    void send(KeyboardInterpreter& interpreter, const ProtocolMessage& message, TimePoint now) {
        QString                     error;
        [[maybe_unused]] const auto actions = interpreter.process(message, now, &error);
        check(error.isEmpty(), "valid keyboard message interpreted without error");
    }

    KeyboardSnapshotMessage snapshot(const KeymapFixture& keymap, std::uint32_t id = 1, std::uint32_t rate = 0, std::uint32_t delay = 0) {
        return {.id = id, .name = QStringLiteral("test keyboard"), .keymap = keymap.text, .repeatRate = rate, .repeatDelay = delay};
    }

    KeyMessage key(std::uint32_t id, std::uint32_t code, bool pressed, std::uint32_t timeMs = 1) {
        return {.keyboardId = id, .timeMs = timeMs, .keycode = code, .pressed = pressed};
    }

    std::vector<InterpretedAction> process(KeyboardInterpreter& interpreter, const ProtocolMessage& message, TimePoint now) {
        QString error;
        auto    actions = interpreter.process(message, now, &error);
        check(error.isEmpty(), "keyboard message interpreted without error");
        return actions;
    }

    void testTextAndXkbOffset() {
        const auto          keymap = makeKeymap("us");
        KeyboardInterpreter interpreter;
        const auto          now = TimePoint{};
        send(interpreter, snapshot(keymap), now);

        auto actions = process(interpreter, key(1, 30, true), now);
        check(actions.size() == 1, "one action emitted for an ordinary key press");
        check(actions.front().kind == InterpretedActionKind::Text && actions.front().text == QStringLiteral("a"), "evdev keycode 30 maps to XKB keycode 38 and produces a");
        check(actions.front().keyboardId == 1 && actions.front().keycode == 30, "action retains keyboard and source key identity");
        check(process(interpreter, key(1, 30, false, 2), now).empty(), "ordinary key release does not append output");
    }

    void testShiftAndStandaloneModifier() {
        const auto          keymap = makeKeymap("us");
        KeyboardInterpreter interpreter;
        const auto          now = TimePoint{};
        send(interpreter, snapshot(keymap), now);

        check(process(interpreter, key(1, 42, true), now).empty(), "modifier press is held rather than appended");
        check(interpreter.heldModifiers() == QStringLiteral("Shift"), "held modifier is exposed immediately");
        send(interpreter, ModifiersMessage{.keyboardId = 1, .depressed = keymap.shiftMask}, now);
        const auto shifted = process(interpreter, key(1, 30, true, 2), now);
        check(shifted.size() == 1 && shifted.front().kind == InterpretedActionKind::Text && shifted.front().text == QStringLiteral("A"),
              "Shift+A produces text A instead of a modifier chord");
        check(shifted.front().modifiers == QStringList{QStringLiteral("Shift")}, "text action retains the modifier identity used for translation");
        check(process(interpreter, key(1, 30, false, 3), now).empty(), "shifted letter release emits nothing");
        check(process(interpreter, key(1, 42, false, 4), now).empty(), "modifier used for text is not emitted as a standalone key");
        send(interpreter, ModifiersMessage{.keyboardId = 1}, now);
        check(interpreter.heldModifiers().isEmpty(), "released modifier is no longer held");

        process(interpreter, key(1, 42, true, 5), now);
        send(interpreter, ModifiersMessage{.keyboardId = 1, .depressed = keymap.shiftMask}, now);
        const auto shiftedNavigation = process(interpreter, key(1, 102, true, 6), now);
        check(shiftedNavigation.size() == 1 && shiftedNavigation.front().kind == InterpretedActionKind::Chord &&
                  shiftedNavigation.front().modifiers == QStringList{QStringLiteral("Shift")} && shiftedNavigation.front().key == QStringLiteral("Home"),
              "unconsumed Shift on a non-text key is preserved as a chord");
        process(interpreter, key(1, 102, false, 7), now);
        process(interpreter, key(1, 42, false, 8), now);
        send(interpreter, ModifiersMessage{.keyboardId = 1}, now);

        check(process(interpreter, key(1, 29, true, 9), now).empty(), "standalone Control press is held");
        check(interpreter.heldModifiers() == QStringLiteral("Ctrl"), "standalone Control is shown while held");
        const auto standalone = process(interpreter, key(1, 29, false, 6), now);
        check(standalone.size() == 1 && standalone.front().kind == InterpretedActionKind::Key && standalone.front().key == QStringLiteral("Ctrl"),
              "unused standalone modifier is emitted on release");
    }

    void testChordsAndPerKeyboardState() {
        const auto          keymap = makeKeymap("us");
        KeyboardInterpreter interpreter;
        const auto          now = TimePoint{};
        send(interpreter, snapshot(keymap, 1), now);
        send(interpreter, snapshot(keymap, 2), now);

        check(process(interpreter, key(1, 29, true), now).empty(), "Control held on first keyboard");
        send(interpreter, ModifiersMessage{.keyboardId = 1, .depressed = keymap.controlMask}, now);
        const auto otherKeyboard = process(interpreter, key(2, 46, true), now);
        check(otherKeyboard.size() == 1 && otherKeyboard.front().kind == InterpretedActionKind::Text && otherKeyboard.front().text == QStringLiteral("c"),
              "modifier state on one keyboard does not leak to another");

        const auto chord = process(interpreter, key(1, 46, true, 2), now);
        check(chord.size() == 1 && chord.front().kind == InterpretedActionKind::Chord, "Control+C is interpreted as a chord");
        check(chord.front().key == QStringLiteral("C") && chord.front().modifiers == QStringList{QStringLiteral("Ctrl")}, "chord retains its key and modifier identities");
        check(process(interpreter, key(1, 46, false, 3), now).empty(), "chord key release emits no second action");
        check(process(interpreter, key(1, 29, false, 4), now).empty(), "modifier used in a chord is not emitted standalone");
    }

    void testLayoutAwareText() {
        const auto          keymap = makeKeymap("de");
        KeyboardInterpreter interpreter;
        const auto          now = TimePoint{};
        send(interpreter, snapshot(keymap), now);

        check(process(interpreter, key(1, 100, true), now).empty(), "AltGr press is held");
        const auto atSign = process(interpreter, key(1, 16, true, 2), now);
        check(atSign.size() == 1 && atSign.front().kind == InterpretedActionKind::Text && atSign.front().text == QStringLiteral("@"),
              "consumed AltGr modifier yields the layout character, not an AltGr chord");
        check(atSign.front().modifiers == QStringList{QStringLiteral("AltGr")}, "layout-produced text retains its consumed AltGr identity");
    }

    void testKeymapAndKeyboardRemovalResetInputState() {
        const auto          keymap = makeKeymap("us");
        KeyboardInterpreter interpreter;
        const auto          now = TimePoint{};
        send(interpreter, snapshot(keymap, 1, 20, 200), now);
        process(interpreter, key(1, 42, true), now);
        process(interpreter, key(1, 30, true), now);
        check(interpreter.heldModifiers() == QStringLiteral("Shift"), "held modifier recorded before keymap replacement");
        check(interpreter.nextRepeatDeadline() != TimePoint::max(), "repeat active before keymap replacement");

        send(interpreter, KeymapMessage{.keyboardId = 1, .keymap = keymap.text}, now + std::chrono::milliseconds(50));
        check(interpreter.heldModifiers().isEmpty(), "keymap replacement clears observed held keys");
        check(interpreter.nextRepeatDeadline() == TimePoint::max(), "keymap replacement cancels repeats");

        process(interpreter, key(1, 30, true, 3), now + std::chrono::milliseconds(60));
        check(interpreter.nextRepeatDeadline() != TimePoint::max(), "new key presses use the replacement keymap");
        send(interpreter, UnsubscribeKeyboardMessage{.id = 1}, now + std::chrono::milliseconds(70));
        check(interpreter.nextRepeatDeadline() == TimePoint::max(), "keyboard removal cancels its repeats");
        check(process(interpreter, key(1, 30, true, 4), now + std::chrono::milliseconds(80)).empty(), "removed keyboard cannot produce further output");
    }

    void testPresenterPreservesHistoryAcrossPauseAndReset() {
        const auto        keymap = makeKeymap("us");
        KeyboardPresenter presenter;
        presenter.processMessage(snapshot(keymap));
        presenter.processMessage(key(1, 30, true));
        check(presenter.outputText() == QStringLiteral("a"), "presenter records interpreted text");

        presenter.processMessage(CastingStateMessage{.paused = true});
        check(presenter.outputText() == QStringLiteral("a"), "pausing input preserves retained history");
        presenter.resetConnection();
        check(presenter.outputText() == QStringLiteral("a"), "connection reset preserves retained history");
    }

    void testDeterministicRepeatsAndCancellation() {
        const auto          keymap = makeKeymap("us");
        KeyboardInterpreter interpreter;
        const auto          start = TimePoint{};
        send(interpreter, snapshot(keymap, 1, 10, 500), start);

        const auto initial = process(interpreter, key(1, 30, true), start);
        check(initial.size() == 1 && !initial.front().repeated, "initial key press is not marked as repeat");
        check(interpreter.advance(start + std::chrono::milliseconds(499)).empty(), "repeat does not fire before configured delay");
        auto repeated = interpreter.advance(start + std::chrono::milliseconds(500));
        check(repeated.size() == 1 && repeated.front().repeated && repeated.front().repeatCount == 1 && repeated.front().text == QStringLiteral("a"),
              "first local repeat fires at the configured delay with original interpretation");
        repeated = interpreter.advance(start + std::chrono::milliseconds(600));
        check(repeated.size() == 1 && repeated.front().repeatCount == 2, "repeat interval follows configured rate");

        send(interpreter, RepeatInfoMessage{.keyboardId = 1, .rate = 20, .delay = 300}, start + std::chrono::milliseconds(650));
        check(interpreter.advance(start + std::chrono::milliseconds(949)).empty(), "repeat setting changes restart the delay for held keys");
        repeated = interpreter.advance(start + std::chrono::milliseconds(950));
        check(repeated.size() == 1 && repeated.front().repeatCount == 3, "updated repeat settings are applied to held keys");

        check(process(interpreter, key(1, 30, false, 3), start + std::chrono::milliseconds(951)).empty(), "release cancels repeat without output");
        check(interpreter.nextRepeatDeadline() == TimePoint::max(), "no repeat remains scheduled after release");
        check(interpreter.advance(start + std::chrono::seconds(10)).empty(), "released key never repeats later");

        process(interpreter, key(1, 30, true, 4), start + std::chrono::seconds(11));
        send(interpreter, CastingStateMessage{.paused = true}, start + std::chrono::seconds(11));
        check(interpreter.heldModifiers().isEmpty(), "pause clears observed held state");
        check(interpreter.nextRepeatDeadline() == TimePoint::max(), "pause cancels active repeats");
        check(interpreter.advance(start + std::chrono::seconds(20)).empty(), "paused key does not repeat");
    }
} // namespace

int main() {
    testTextAndXkbOffset();
    testShiftAndStandaloneModifier();
    testChordsAndPerKeyboardState();
    testLayoutAwareText();
    testKeymapAndKeyboardRemovalResetInputState();
    testPresenterPreservesHistoryAcrossPauseAndReset();
    testDeterministicRepeatsAndCancellation();
    std::cout << "overlay input tests passed\n";
    return 0;
}
