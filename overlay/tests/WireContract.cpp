// What the plugin's encoder writes must be what the overlay's parser reads:
// each message type goes through both, and every field has to survive.
#include "hyprcast/protocol/key_state.hpp"
#include "hyprcast/protocol/keyboard_snapshot.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/repeat_info.hpp"
#include "hyprcast/wire/messages.hpp"
#include "ipc/Protocol.hpp"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace hyprcast;
using namespace hyprcast::overlay;

constexpr auto uint32Max = std::numeric_limits<std::uint32_t>::max();
// The overlay hands repeat settings to APIs that take int.
constexpr auto intMax =
    static_cast<std::uint32_t>(std::numeric_limits<int>::max());

// A keymap as libxkbcommon prints it: several lines, tabs and quoted names.
constexpr std::string_view keymapText =
    "xkb_keymap {\n"
    "\txkb_keycodes \"evdev+aliases(qwerty)\" {\n"
    "\t\t<ESC> = 9;\n"
    "\t};\n"
    "};\n";

// Parses `line` as the overlay does and returns the message it must hold.
template <typename Message>
Message parsed(const std::string& line) {
    const auto message = parseMessage(QByteArray::fromStdString(line));
    if (!message) {
        ADD_FAILURE() << "the overlay rejects " << line << ": "
                      << message.error().toStdString();
        return {};
    }
    const auto* typed = std::get_if<Message>(&*message);
    if (typed == nullptr) {
        ADD_FAILURE() << "the overlay reads " << line
                      << " as another message type";
        return {};
    }
    return *typed;
}

void expectEqual(const modifier_state& actual, const modifier_state& expected) {
    EXPECT_EQ(actual.depressed, expected.depressed);
    EXPECT_EQ(actual.latched, expected.latched);
    EXPECT_EQ(actual.locked, expected.locked);
    EXPECT_EQ(actual.group, expected.group);
}

void expectEqual(const repeat_info& actual, const repeat_info& expected) {
    EXPECT_EQ(actual.rate, expected.rate);
    EXPECT_EQ(actual.delay, expected.delay);
}

TEST(WireContract, CastingState) {
    EXPECT_TRUE(
        parsed<CastingStateMessage>(wire::casting_state_message(true)).paused);
    EXPECT_FALSE(
        parsed<CastingStateMessage>(wire::casting_state_message(false)).paused);
}

TEST(WireContract, SubscribeKeyboard) {
    const keyboard_snapshot snapshot {
        .id = 7,
        .name = "Clavier \"dvorak\" à 62 touches",
        .modifiers = {.depressed = 1, .latched = 2, .locked = 16, .group = 3},
        .keymap = std::string {keymapText},
        .repeat_info = {.rate = 25, .delay = 600},
    };

    const auto message =
        parsed<KeyboardSnapshotMessage>(wire::subscribe_message(snapshot));

    EXPECT_EQ(message.id, snapshot.id);
    EXPECT_EQ(message.name, QString::fromStdString(snapshot.name));
    expectEqual(message.modifiers, snapshot.modifiers);
    EXPECT_EQ(message.keymap, QString::fromStdString(snapshot.keymap));
    expectEqual(message.repeatInfo, snapshot.repeat_info);
}

TEST(WireContract, UnsubscribeKeyboard) {
    EXPECT_EQ(
        parsed<UnsubscribeKeyboardMessage>(wire::unsubscribe_message(7)).id,
        7u);
    EXPECT_EQ(
        parsed<UnsubscribeKeyboardMessage>(wire::unsubscribe_message(uint32Max))
            .id,
        uint32Max);
}

TEST(WireContract, Key) {
    const auto press = parsed<KeyMessage>(wire::key_message(
        3,
        {.time_ms = 123456, .keycode = 30, .state = key_state::pressed}));
    EXPECT_EQ(press.keyboardId, 3u);
    EXPECT_EQ(press.timeMs, 123456u);
    EXPECT_EQ(press.keycode, 30u);
    EXPECT_TRUE(press.pressed);

    const auto release =
        parsed<KeyMessage>(wire::key_message(uint32Max,
                                             {
                                                 .time_ms = uint32Max,
                                                 .keycode = uint32Max,
                                                 .state = key_state::released,
                                             }));
    EXPECT_EQ(release.keyboardId, uint32Max);
    EXPECT_EQ(release.timeMs, uint32Max);
    EXPECT_EQ(release.keycode, uint32Max);
    EXPECT_FALSE(release.pressed);
}

TEST(WireContract, Modifiers) {
    const modifier_state modifiers {
        .depressed = 5,
        .latched = 0,
        .locked = uint32Max,
        .group = 2,
    };

    const auto message =
        parsed<ModifiersMessage>(wire::modifiers_message(3, modifiers));

    EXPECT_EQ(message.keyboardId, 3u);
    expectEqual(message.modifiers, modifiers);
}

TEST(WireContract, Keymap) {
    const std::string keymap {keymapText};

    const auto message = parsed<KeymapMessage>(wire::keymap_message(3, keymap));

    EXPECT_EQ(message.keyboardId, 3u);
    EXPECT_EQ(message.keymap, QString::fromStdString(keymap));
}

TEST(WireContract, RepeatInfo) {
    for (const repeat_info info : {
             repeat_info {.rate = 25, .delay = 600},
             // Repeat switched off.
             repeat_info {.rate = 0, .delay = 0},
             // The largest values the compositor can report.
             repeat_info {.rate = intMax, .delay = intMax},
         }) {
        SCOPED_TRACE(info.rate);

        const auto message =
            parsed<RepeatInfoMessage>(wire::repeat_info_message(3, info));

        EXPECT_EQ(message.keyboardId, 3u);
        expectEqual(message.repeatInfo, info);
    }
}

} // namespace
