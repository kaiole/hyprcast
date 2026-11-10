#include "hyprcast/protocol/key_state.hpp"
#include "hyprcast/wire/messages.hpp"
#include "libjson/json_value.hpp"
#include "libjson/parser.hpp"
#include "libjson/serializer.hpp"

#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace hyprcast;

// The encoder keeps members in an unordered map, so their order on the wire is
// unspecified. Golden lines are therefore compared with the members sorted by
// name; everything else (spelling, escaping, number format) is compared as
// written.
std::string sorted_members(std::string_view message) {
    // One message is one line: the server appends the only newline.
    EXPECT_EQ(message.find('\n'), std::string_view::npos) << message;

    const auto parsed = libjson::parser {message}.parse();
    if (!parsed) {
        ADD_FAILURE() << "not JSON: " << message;
        return {};
    }
    const auto* object =
        std::get_if<libjson::json_value::object>(&parsed->data);
    if (object == nullptr) {
        ADD_FAILURE() << "not a JSON object: " << message;
        return {};
    }

    std::map<std::string, std::string> members;
    for (const auto& [name, value] : *object) {
        members.emplace(libjson::serialize({.data = name}),
                        libjson::serialize(value));
    }

    std::string result {"{"};
    for (const auto& [name, value] : members) {
        if (result.size() > 1) {
            result += ',';
        }
        result += name;
        result += ':';
        result += value;
    }
    result += '}';
    return result;
}

TEST(WireMessages, CastingState) {
    EXPECT_EQ(sorted_members(wire::casting_state_message(true)),
              R"({"event":"casting_state","keyboard_id":0,"paused":true})");
    EXPECT_EQ(sorted_members(wire::casting_state_message(false)),
              R"({"event":"casting_state","keyboard_id":0,"paused":false})");
}

TEST(WireMessages, SubscribeKeyboard) {
    const std::string message = wire::subscribe_message({
        .id = 3,
        .name = "AT \"Set 2\" keyboard",
        .modifiers = {.depressed = 1, .latched = 2, .locked = 16, .group = 1},
        .keymap = "xkb_keymap {\n\txkb_keycodes \"evdev\" { };\n};\n",
        .repeat_info = {.rate = 25, .delay = 600},
    });

    EXPECT_EQ(sorted_members(message),
              R"({"delay":600,"depressed":1,"event":"subscribe_keyboard",)"
              R"("group":1,"keyboard_id":3,)"
              R"("keymap":"xkb_keymap {\n\txkb_keycodes \"evdev\" { };\n};\n",)"
              R"("latched":2,"locked":16,"name":"AT \"Set 2\" keyboard",)"
              R"("rate":25})");
}

TEST(WireMessages, UnsubscribeKeyboard) {
    EXPECT_EQ(sorted_members(wire::unsubscribe_message(7)),
              R"({"event":"unsubscribe_keyboard","keyboard_id":7})");
}

TEST(WireMessages, Key) {
    EXPECT_EQ(sorted_members(wire::key_message(2,
                                               {
                                                   .time_ms = 123456,
                                                   .keycode = 30,
                                                   .state = key_state::pressed,
                                               })),
              R"({"event":"key","keyboard_id":2,"keycode":30,)"
              R"("state":"pressed","time_ms":123456})");
    EXPECT_EQ(sorted_members(wire::key_message(4294967295,
                                               {
                                                   .time_ms = 4294967295,
                                                   .keycode = 4294967295,
                                                   .state = key_state::released,
                                               })),
              R"({"event":"key","keyboard_id":4294967295,)"
              R"("keycode":4294967295,"state":"released",)"
              R"("time_ms":4294967295})");
}

TEST(WireMessages, Modifiers) {
    EXPECT_EQ(sorted_members(wire::modifiers_message(2,
                                                     {
                                                         .depressed = 5,
                                                         .latched = 8,
                                                         .locked = 16,
                                                         .group = 3,
                                                     })),
              R"({"depressed":5,"event":"modifiers","group":3,)"
              R"("keyboard_id":2,"latched":8,"locked":16})");
}

TEST(WireMessages, Keymap) {
    EXPECT_EQ(sorted_members(wire::keymap_message(2, "a\\b\r\n\x01 \"c\"")),
              R"({"event":"keymap","keyboard_id":2,)"
              R"("keymap":"a\\b\r\n\u0001 \"c\""})");
}

TEST(WireMessages, RepeatInfo) {
    EXPECT_EQ(
        sorted_members(wire::repeat_info_message(2, {.rate = 0, .delay = 250})),
        R"({"delay":250,"event":"repeat_info","keyboard_id":2,"rate":0})");
}

TEST(WireMessages, PassesUtf8ThroughAndRejectsInvalidBytes) {
    EXPECT_EQ(sorted_members(wire::subscribe_message({
                  .id = 1,
                  .name = "Clavier d’été",
                  .modifiers = {},
                  .keymap = "k",
                  .repeat_info = {},
              })),
              R"({"delay":0,"depressed":0,"event":"subscribe_keyboard",)"
              R"("group":0,"keyboard_id":1,"keymap":"k","latched":0,)"
              R"("locked":0,"name":"Clavier d’été","rate":0})");

    // A string that is not UTF-8 cannot be encoded; the caller's callback
    // boundary turns the exception into a client disconnect.
    EXPECT_THROW(static_cast<void>(wire::keymap_message(1, "\xff")),
                 std::invalid_argument);
}

} // namespace
