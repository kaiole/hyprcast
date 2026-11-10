#include "hyprcast/session/session.hpp"

#include "hyprcast/log/log_sink.hpp"
#include "hyprcast/protocol/key_event.hpp"
#include "hyprcast/protocol/key_state.hpp"
#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/keyboard_snapshot.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/registry_snapshot.hpp"
#include "hyprcast/protocol/repeat_info.hpp"
#include "hyprcast/test_support/event_loop.hpp"
#include "hyprcast/test_support/recording_log.hpp"
#include "hyprcast/test_support/socket_client.hpp"
#include "hyprcast/test_support/temp_dir.hpp"
#include "hyprcast/wire/messages.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

using namespace hyprcast;
using test_support::socket_client;

using line_list = std::vector<std::string>;

keyboard_snapshot keyboard(keyboard_id id, const std::string& name) {
    return {
        .id = id,
        .name = name,
        .modifiers = {.depressed = 1, .latched = 0, .locked = 2, .group = 0},
        .keymap = "xkb_keymap { " + name + " };",
        .repeat_info = {.rate = 25, .delay = 600},
    };
}

constexpr key_event press_a {
    .time_ms = 100,
    .keycode = 30,
    .state = key_state::pressed,
};
constexpr key_event release_a {
    .time_ms = 180,
    .keycode = 30,
    .state = key_state::released,
};

// Runs a real session on a real event loop and socket, in a directory of its
// own. `keyboards` is what the keyboard source reports when asked. Expected
// lines come from the wire encoder, whose format the wire tests pin.
class SessionTest : public testing::Test {
protected:
    [[nodiscard]] std::filesystem::path sock_path() const {
        return dir.path() / "events.sock";
    }

    // Connects and waits until the session has asked for the keyboards, which
    // it does once per accepted client.
    [[nodiscard]] socket_client connect() {
        const int snapshots_before = snapshots_taken;
        socket_client client {sock_path()};
        EXPECT_TRUE(wait([&] { return snapshots_taken > snapshots_before; }))
            << "the session did not accept the connection";
        return client;
    }

    [[nodiscard]] bool wait(const std::function<bool()>& done) {
        return loop.run_until(done);
    }

    // Waits for `expected.size()` lines and compares everything received.
    void expect_lines(socket_client& client, const line_list& expected) {
        EXPECT_TRUE(wait([&] {
            return client.lines().size() >= expected.size();
        })) << "the client received "
            << client.lines().size() << " of " << expected.size() << " lines";
        EXPECT_EQ(client.lines(), expected);
    }

    // Sends a command and waits for the echo that answers it, so that what
    // the test does next happens after the session has handled the command.
    void send_command(socket_client& client,
                      const std::string& command,
                      bool paused_afterwards) {
        const auto echo = wire::casting_state_message(paused_afterwards);
        const auto lines_before = client.lines().size();
        client.send(command + "\n");
        ASSERT_TRUE(wait([&] {
            const auto lines = client.lines();
            return lines.size() > lines_before && lines.back() == echo;
        })) << "no answer to "
            << command;
        EXPECT_EQ(under_test.paused(), paused_afterwards);
    }

    test_support::temp_dir dir;
    test_support::recording_log log;
    registry_snapshot keyboards;
    std::function<void()> on_snapshot;
    int snapshots_taken = 0;
    // The loop outlives the session, which registers with it.
    test_support::event_loop loop;
    session under_test {
        loop.get(),
        dir.path(),
        log,
        [this] {
            ++snapshots_taken;
            if (on_snapshot) {
                on_snapshot();
            }
            return keyboards;
        },
    };
};

TEST_F(SessionTest, StartsPausedWithoutAskingForKeyboards) {
    EXPECT_TRUE(under_test.paused());
    EXPECT_EQ(snapshots_taken, 0);
    EXPECT_TRUE(std::filesystem::is_socket(sock_path()));
}

TEST_F(SessionTest, TellsANewClientTheStateAndEveryKeyboard) {
    keyboards = {keyboard(1, "first"), keyboard(2, "second")};

    auto client = connect();

    expect_lines(client,
                 {
                     wire::casting_state_message(true),
                     wire::subscribe_message(keyboard(1, "first")),
                     wire::subscribe_message(keyboard(2, "second")),
                 });
    EXPECT_EQ(snapshots_taken, 1);
}

TEST_F(SessionTest, TellsANewClientTheStateWhenThereAreNoKeyboards) {
    auto client = connect();

    expect_lines(client, {wire::casting_state_message(true)});
}

TEST_F(SessionTest, SendsNoKeyEventsWhilePaused) {
    auto client = connect();

    under_test.on_key_event(1, press_a);
    send_command(client, "enable", false);
    under_test.on_key_event(1, release_a);

    // The press would have arrived before the answer to `enable`.
    expect_lines(client,
                 {
                     wire::casting_state_message(true),
                     wire::casting_state_message(false),
                     wire::key_message(1, release_a),
                 });
}

TEST_F(SessionTest, SendsKeyEventsAfterEnable) {
    auto client = connect();
    send_command(client, "enable", false);

    under_test.on_key_event(1, press_a);
    under_test.on_key_event(2, press_a);
    under_test.on_key_event(1, release_a);

    expect_lines(client,
                 {
                     wire::casting_state_message(true),
                     wire::casting_state_message(false),
                     wire::key_message(1, press_a),
                     wire::key_message(2, press_a),
                     wire::key_message(1, release_a),
                 });
}

TEST_F(SessionTest, DisablePausesAgain) {
    auto client = connect();
    send_command(client, "enable", false);

    send_command(client, "disable", true);
    under_test.on_key_event(1, press_a);
    send_command(client, "enable", false);

    expect_lines(client,
                 {
                     wire::casting_state_message(true),
                     wire::casting_state_message(false),
                     wire::casting_state_message(true),
                     wire::casting_state_message(false),
                 });
}

TEST_F(SessionTest, AnswersEveryCommandEvenWithoutAChange) {
    auto client = connect();

    client.send("disable\nenable\nenable\n");

    expect_lines(client,
                 {
                     wire::casting_state_message(true),
                     wire::casting_state_message(true),
                     wire::casting_state_message(false),
                     wire::casting_state_message(false),
                 });
    EXPECT_FALSE(under_test.paused());
}

// Capture is the client's to turn on: nothing on the compositor side can.
template <typename Session>
concept can_be_unpaused_directly =
    requires(Session& target) { target.set_paused(false); };

TEST_F(SessionTest, OnlyAClientCommandUnpauses) {
    EXPECT_FALSE(can_be_unpaused_directly<session>);
}

TEST_F(SessionTest, PausesWhenTheClientLeaves) {
    auto first = connect();
    send_command(first, "enable", false);

    first.close();
    EXPECT_TRUE(wait([&] { return under_test.paused(); }));

    // The next client starts paused and has to ask again.
    auto second = connect();
    under_test.on_key_event(1, press_a);
    send_command(second, "enable", false);
    under_test.on_key_event(1, release_a);

    expect_lines(second,
                 {
                     wire::casting_state_message(true),
                     wire::casting_state_message(false),
                     wire::key_message(1, release_a),
                 });
}

TEST_F(SessionTest, PausesWhenANewClientReplacesTheCurrentOne) {
    auto first = connect();
    send_command(first, "enable", false);

    auto second = connect();

    EXPECT_TRUE(under_test.paused());
    EXPECT_TRUE(wait([&] { return first.closed(); }));
    under_test.on_key_event(1, press_a);
    send_command(second, "enable", false);

    expect_lines(second,
                 {
                     wire::casting_state_message(true),
                     wire::casting_state_message(false),
                 });
}

TEST_F(SessionTest, PausesWhenTheClientIsDisconnected) {
    auto client = connect();
    send_command(client, "enable", false);

    under_test.disconnect_client();

    EXPECT_TRUE(under_test.paused());
    EXPECT_TRUE(wait([&] { return client.closed(); }));
}

TEST_F(SessionTest, ForwardsKeyboardsComingAndGoing) {
    keyboards = {keyboard(1, "first")};
    auto client = connect();

    under_test.on_subscribe(keyboard(2, "second"));
    under_test.on_unsubscribe(1);

    expect_lines(client,
                 {
                     wire::casting_state_message(true),
                     wire::subscribe_message(keyboard(1, "first")),
                     wire::subscribe_message(keyboard(2, "second")),
                     wire::unsubscribe_message(1),
                 });
}

TEST_F(SessionTest, ForwardsKeyboardStateChangesAfterEnable) {
    auto client = connect();
    send_command(client, "enable", false);
    const modifier_state modifiers {
        .depressed = 4,
        .latched = 0,
        .locked = 2,
        .group = 1,
    };
    const repeat_info repeat {.rate = 40, .delay = 250};

    under_test.on_modifiers_event(1, modifiers);
    under_test.on_keymap_event(1, "xkb_keymap { new };");
    under_test.on_repeat_info_event(1, repeat);

    expect_lines(client,
                 {
                     wire::casting_state_message(true),
                     wire::casting_state_message(false),
                     wire::modifiers_message(1, modifiers),
                     wire::keymap_message(1, "xkb_keymap { new };"),
                     wire::repeat_info_message(1, repeat),
                 });
}

TEST_F(SessionTest, DropsEverythingWhileNoClientIsConnected) {
    under_test.on_subscribe(keyboard(1, "first"));
    under_test.on_key_event(1, press_a);
    under_test.on_unsubscribe(1);

    auto client = connect();
    send_command(client, "enable", false);
    under_test.on_key_event(1, release_a);

    expect_lines(client,
                 {
                     wire::casting_state_message(true),
                     wire::casting_state_message(false),
                     wire::key_message(1, release_a),
                 });
}

TEST_F(SessionTest, DisconnectsAClientWhoseKeyboardsCannotBeListed) {
    on_snapshot = [] { throw std::runtime_error("no keyboards today"); };

    auto failed = connect();

    EXPECT_TRUE(wait([&] { return failed.closed(); }));
    EXPECT_TRUE(log.contains(log_level::error, "no keyboards today"));

    // The session itself carries on with the next client.
    on_snapshot = nullptr;
    auto client = connect();
    expect_lines(client, {wire::casting_state_message(true)});
}

// A name that is not UTF-8 cannot be encoded. The caller decides what that
// means for the client; the session keeps running.
TEST_F(SessionTest, ReportsAnUnencodableMessageToTheCaller) {
    auto client = connect();

    EXPECT_THROW(under_test.on_subscribe(keyboard(1, "\xff")),
                 std::invalid_argument);

    under_test.on_unsubscribe(1);
    expect_lines(client,
                 {
                     wire::casting_state_message(true),
                     wire::unsubscribe_message(1),
                 });
}

TEST_F(SessionTest, ShutdownDropsTheClientAndPauses) {
    auto client = connect();
    send_command(client, "enable", false);

    under_test.shutdown();

    EXPECT_TRUE(under_test.paused());
    EXPECT_TRUE(wait([&] { return client.closed(); }));
    EXPECT_THROW(socket_client {sock_path()}, std::system_error);
    EXPECT_NO_THROW(under_test.on_key_event(1, press_a));
}

} // namespace
