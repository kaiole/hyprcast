#include "hyprcast/log/log_sink.hpp"
#include "hyprcast/os/unique_fd.hpp"
#include "hyprcast/server/event_server.hpp"
#include "hyprcast/test_support/event_loop.hpp"
#include "hyprcast/test_support/recording_log.hpp"
#include "hyprcast/test_support/socket_client.hpp"
#include "hyprcast/test_support/temp_dir.hpp"

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-server-core.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

struct wl_event_source;

namespace {

// How often the server changed the events a descriptor is watched for.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
int fd_update_count = 0;

} // namespace

// Stands in front of libwayland's function of the same name, which the server
// library then calls through here: the only trace such an update leaves is
// the call itself.
extern "C" int wl_event_source_fd_update(::wl_event_source* source,
                                         std::uint32_t mask) {
    using fd_update_fn = int (*)(::wl_event_source*, std::uint32_t);
    // dlsym() hands every symbol back as a void*.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    static const auto next = reinterpret_cast<fd_update_fn>(
        ::dlsym(RTLD_NEXT, "wl_event_source_fd_update"));

    ++fd_update_count;
    return next(source, mask);
}

namespace {

using namespace hyprcast;
using test_support::socket_client;

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file {path};
    return {std::istreambuf_iterator<char> {file}, {}};
}

// An upper bound on what a Unix socket takes off a sender whose peer does not
// read: the kernel stops at the send buffer size, give or take one chunk of
// at most half of it.
std::size_t socket_buffer_allowance() {
    const unique_fd sock_fd {::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    int send_buffer_size = 0;
    ::socklen_t option_size = sizeof(send_buffer_size);
    if (!sock_fd
        || ::getsockopt(sock_fd.get(),
                        SOL_SOCKET,
                        SO_SNDBUF,
                        &send_buffer_size,
                        &option_size)
            == -1) {
        throw std::system_error(errno,
                                std::generic_category(),
                                "Cannot read the socket send buffer size");
    }
    return 2 * static_cast<std::size_t>(send_buffer_size);
}

// Runs a real event_server on a real event loop and socket, in a directory of
// its own. `calls` records the server's callbacks in order.
class EventServerTest : public testing::Test {
protected:
    using call_list = std::vector<std::string>;

    void start() { start_in(sock_dir()); }

    void start_in(const std::filesystem::path& directory) {
        server = std::make_unique<event_server>(
            loop.get(),
            directory,
            log,
            [this] {
                calls.emplace_back("connected");
                if (on_connected) {
                    on_connected();
                }
            },
            [this](bool enabled) {
                calls.emplace_back(enabled ? "enable" : "disable");
                if (on_capture) {
                    on_capture(enabled);
                }
            },
            [this] { calls.emplace_back("disconnected"); });
    }

    // Two levels below the temporary directory, like the real
    // $XDG_RUNTIME_DIR/hyprcast/<instance signature>.
    [[nodiscard]] std::filesystem::path sock_dir() const {
        return dir.path() / "hyprcast" / "instance";
    }
    [[nodiscard]] std::filesystem::path sock_path() const {
        return sock_dir() / "events.sock";
    }
    [[nodiscard]] std::filesystem::path lock_path() const {
        return sock_dir() / "server.lock";
    }

    // Connects and waits until the server has accepted the connection.
    [[nodiscard]] socket_client connect() {
        const auto connected_before = count("connected");
        socket_client client {sock_path()};
        EXPECT_TRUE(wait([&] { return count("connected") > connected_before; }))
            << "the server did not accept the connection";
        return client;
    }

    [[nodiscard]] bool wait(const std::function<bool()>& done) {
        return loop.run_until(done);
    }

    [[nodiscard]] bool wait_for_calls(const call_list& expected) {
        return wait([&] { return calls.size() >= expected.size(); })
            && calls == expected;
    }

    [[nodiscard]] std::ptrdiff_t count(const std::string& call) const {
        return std::ranges::count(calls, call);
    }

    test_support::temp_dir dir;
    test_support::recording_log log;
    call_list calls;
    std::function<void()> on_connected;
    std::function<void(bool)> on_capture;
    // The loop outlives the server, which registers with it.
    test_support::event_loop loop;
    std::unique_ptr<event_server> server;
};

TEST(EventSocketDir, IsTheInstanceDirectoryBelowTheRuntimeDirectory) {
    EXPECT_EQ(event_socket_dir("/run/user/1000", "abc_123"),
              std::filesystem::path {"/run/user/1000/hyprcast/abc_123"});
}

TEST(EventSocketDir, NeedsARuntimeDirectory) {
    EXPECT_THROW(static_cast<void>(event_socket_dir(nullptr, "abc")),
                 std::runtime_error);
    EXPECT_THROW(static_cast<void>(event_socket_dir("", "abc")),
                 std::runtime_error);
}

TEST_F(EventServerTest, CreatesTheDirectoryTheSocketAndTheLock) {
    ASSERT_FALSE(std::filesystem::exists(sock_dir()));

    start();

    EXPECT_TRUE(std::filesystem::is_directory(sock_dir()));
    EXPECT_TRUE(std::filesystem::is_socket(sock_path()));
    EXPECT_TRUE(std::filesystem::is_regular_file(lock_path()));
    EXPECT_TRUE(calls.empty());
}

TEST_F(EventServerTest, RemovesAStaleSocket) {
    std::filesystem::create_directories(sock_dir());
    // Bound and closed again: the file stays, nothing listens on it.
    static_cast<void>(test_support::bind_unix_socket(sock_path()));
    ASSERT_TRUE(std::filesystem::is_socket(sock_path()));
    ASSERT_THROW(socket_client {sock_path()}, std::system_error);

    start();

    auto client = connect();
    EXPECT_TRUE(wait_for_calls({"connected"}));
}

TEST_F(EventServerTest, RefusesToStartWhenThePathIsNotASocket) {
    std::filesystem::create_directories(sock_dir());
    std::ofstream {sock_path()} << "precious";

    try {
        start();
        FAIL() << "the server replaced a regular file";
    } catch (const std::runtime_error& error) {
        EXPECT_TRUE(std::string {error.what()}.contains(
            "Refusing to remove non-socket entry"))
            << error.what();
    }

    EXPECT_TRUE(std::filesystem::is_regular_file(sock_path()));
    EXPECT_EQ(read_file(sock_path()), "precious");

    // The failed attempt holds on to nothing: once the entry is gone a server
    // starts.
    std::filesystem::remove(sock_path());
    EXPECT_NO_THROW(start());
}

TEST_F(EventServerTest, SecondServerOnTheSameDirectoryFailsOnTheLock) {
    start();

    try {
        const event_server
            second {loop.get(), sock_dir(), log, [] {}, [](bool) {}, [] {}};
        FAIL() << "two servers share one directory";
    } catch (const std::system_error& error) {
        EXPECT_TRUE(
            std::string {error.what()}.contains("Cannot acquire server lock"))
            << error.what();
    }

    // The first server is untouched: its socket is still there and served.
    EXPECT_TRUE(std::filesystem::is_socket(sock_path()));
    auto client = connect();
    server->queue_message("still here");
    EXPECT_TRUE(wait([&] { return client.lines().size() == 1; }));
}

TEST_F(EventServerTest, DestructionRemovesTheSocketAndReleasesTheLock) {
    start();
    auto client = connect();

    server.reset();

    EXPECT_FALSE(std::filesystem::exists(sock_path()));
    // The lock file itself stays; only the lock on it is given up.
    EXPECT_TRUE(std::filesystem::is_regular_file(lock_path()));
    EXPECT_TRUE(wait([&] { return client.closed(); }));
    // Unlike shutdown(), destruction does not call back about the client.
    EXPECT_EQ(calls, call_list({"connected"}));

    EXPECT_NO_THROW(start());
    EXPECT_TRUE(std::filesystem::is_socket(sock_path()));
}

TEST_F(EventServerTest, RefusesASocketPathBeyondTheUnixLimit) {
    const auto long_dir = dir.path() / std::string(120, 'x');

    try {
        start_in(long_dir);
        FAIL() << "the server accepted a path that cannot be bound";
    } catch (const std::runtime_error& error) {
        EXPECT_TRUE(std::string {error.what()}.contains(
            "IPC socket path exceeds Unix socket limit"))
            << error.what();
    }

    EXPECT_FALSE(std::filesystem::exists(long_dir));
}

TEST_F(EventServerTest, ReportsAClientConnectingAndLeaving) {
    start();

    auto client = connect();
    EXPECT_EQ(calls, call_list({"connected"}));

    client.close();
    EXPECT_TRUE(wait_for_calls({"connected", "disconnected"}));
}

TEST_F(EventServerTest, SendsEachMessageAsOneLine) {
    start();
    auto client = connect();

    server->queue_message("first");
    server->queue_message("{\"second\":2}");

    EXPECT_TRUE(wait([&] { return client.lines().size() == 2; }));
    EXPECT_EQ(client.received(), "first\n{\"second\":2}\n");
}

TEST_F(EventServerTest, DropsMessagesWhileNoClientIsConnected) {
    start();

    server->queue_message("nobody listens");
    auto client = connect();
    server->queue_message("somebody listens");

    EXPECT_TRUE(wait([&] { return !client.lines().empty(); }));
    EXPECT_EQ(client.received(), "somebody listens\n");
}

TEST_F(EventServerTest, NewClientReplacesTheExistingOne) {
    start();
    auto first = connect();

    auto second = connect();

    EXPECT_EQ(calls, call_list({"connected", "disconnected", "connected"}));
    EXPECT_TRUE(wait([&] { return first.closed(); }));

    server->queue_message("for the new client");
    EXPECT_TRUE(wait([&] { return !second.lines().empty(); }));
    EXPECT_EQ(second.received(), "for the new client\n");
    EXPECT_EQ(first.received(), "");
    EXPECT_FALSE(second.closed());
}

TEST_F(EventServerTest, ForwardsEnableAndDisable) {
    start();
    auto client = connect();

    client.send("enable\n");
    EXPECT_TRUE(wait_for_calls({"connected", "enable"}));

    // Several commands in one write.
    client.send("disable\nenable\n");
    EXPECT_TRUE(wait_for_calls({"connected", "enable", "disable", "enable"}));

    // One command across two writes.
    client.send("disa");
    loop.dispatch_pending();
    client.send("ble\n");
    EXPECT_TRUE(wait_for_calls(
        {"connected", "enable", "disable", "enable", "disable"}));

    EXPECT_FALSE(client.closed());
}

TEST_F(EventServerTest, AnythingButEnableOrDisableDisconnectsTheClient) {
    start();

    for (const std::string command : {
             "status\n",
             "ENABLE\n",
             "enable \n",
             "enable\r\n",
             "\n",
             // Too long to become a command, even without a line end.
             "enable-everything",
         }) {
        SCOPED_TRACE(command);
        calls.clear();
        auto client = connect();

        client.send(command);

        EXPECT_TRUE(wait([&] { return client.closed(); }));
        EXPECT_EQ(calls, call_list({"connected", "disconnected"}));
    }
}

TEST_F(EventServerTest, HandlesCommandsBeforeAnInvalidOne) {
    start();
    auto client = connect();

    client.send("enable\nbogus\ndisable\n");

    EXPECT_TRUE(wait([&] { return client.closed(); }));
    EXPECT_EQ(calls, call_list({"connected", "enable", "disconnected"}));
}

TEST_F(EventServerTest, FailureInTheConnectedCallbackDisconnectsTheClient) {
    on_connected = [] { throw std::runtime_error("no snapshot"); };
    start();

    socket_client client {sock_path()};

    EXPECT_TRUE(wait([&] { return client.closed(); }));
    EXPECT_EQ(calls, call_list({"connected", "disconnected"}));
    EXPECT_TRUE(log.contains(log_level::error, "no snapshot"));
}

TEST_F(EventServerTest, FailureInTheCaptureCallbackDisconnectsTheClient) {
    on_capture = [](bool) { throw std::runtime_error("cannot capture"); };
    start();
    auto client = connect();

    client.send("enable\n");

    EXPECT_TRUE(wait([&] { return client.closed(); }));
    EXPECT_EQ(calls, call_list({"connected", "enable", "disconnected"}));
    EXPECT_TRUE(log.contains(log_level::error, "cannot capture"));
}

TEST_F(EventServerTest, DisconnectClientDropsTheClientOnce) {
    start();
    auto client = connect();

    server->disconnect_client();
    server->disconnect_client();

    EXPECT_EQ(calls, call_list({"connected", "disconnected"}));
    EXPECT_TRUE(wait([&] { return client.closed(); }));
}

// A message that does not fit into the socket buffer is sent in pieces as the
// client reads.
TEST_F(EventServerTest, DeliversAMessageLargerThanTheSocketBuffer) {
    start();
    auto client = connect();
    const std::string large(std::size_t {512} * 1024, 'k');

    server->queue_message(large);
    server->queue_message("after");

    EXPECT_TRUE(wait([&] { return client.received().ends_with("after\n"); }));
    EXPECT_EQ(client.received(), large + "\nafter\n");
    EXPECT_FALSE(client.closed());
}

// Changing the watched events is a system call, so the server only does it
// when a client starts or stops being behind, not with every message.
TEST_F(EventServerTest, ChangesTheWatchedEventsOnlyWhenTheClientFallsBehind) {
    start();
    auto client = connect();
    constexpr int messages = 100;
    const int updates_before = fd_update_count;

    for (int index = 0; index < messages; ++index) {
        server->queue_message("fits into the socket");
    }
    EXPECT_EQ(fd_update_count - updates_before, 0);

    // More than the socket takes: the client is now behind, and stays behind
    // while more messages queue up.
    server->queue_message(std::string(std::size_t {512} * 1024, 'k'));
    for (int index = 0; index < messages; ++index) {
        server->queue_message("queued");
    }
    server->queue_message("caught up");
    EXPECT_EQ(fd_update_count - updates_before, 1);

    // Catching up takes several rounds of the client reading and the server
    // sending more, and ends with one change back.
    EXPECT_TRUE(
        wait([&] { return client.received().ends_with("caught up\n"); }));
    EXPECT_EQ(fd_update_count - updates_before, 2);

    for (int index = 0; index < messages; ++index) {
        server->queue_message("fits again");
    }
    EXPECT_EQ(fd_update_count - updates_before, 2);
    EXPECT_FALSE(client.closed());
}

// The cap on the backlog counts bytes, not messages: a burst of small messages
// behind a client that is not reading yet is held and then delivered.
TEST_F(EventServerTest, HoldsManySmallMessagesForASlowClient) {
    start();
    auto client = connect();
    // More than the socket takes, so that everything after it has to queue.
    const std::string large(std::size_t {512} * 1024, 'k');
    constexpr int small_messages = 2000;

    server->queue_message(large);
    std::string expected = large + "\n";
    for (int index = 0; index < small_messages; ++index) {
        const auto message = std::format("key {}", index);
        server->queue_message(message);
        expected += message + "\n";
    }

    EXPECT_EQ(calls, call_list({"connected"}));
    EXPECT_TRUE(
        wait([&] { return client.received().size() >= expected.size(); }));
    // Not EXPECT_EQ: a failure would print both half-megabyte strings.
    EXPECT_TRUE(client.received() == expected);
    EXPECT_FALSE(client.closed());
}

// The backlog for a client that does not read is bounded: the server holds
// max_queued_bytes at most and gives up on the client with the first message
// that does not fit.
TEST_F(EventServerTest, DisconnectsAClientWhoseBacklogExceedsTheCap) {
    start();
    auto client = connect();
    const std::string message((std::size_t {64} * 1024) - 1, 'k');
    const std::size_t line_size = message.size() + 1;

    std::size_t accepted = 0;
    while (true) {
        server->queue_message(message);
        if (count("disconnected") != 0) {
            break;
        }
        accepted += line_size;
        ASSERT_LT(accepted, 4 * event_server::max_queued_bytes)
            << "the backlog grows without limit";
    }

    // The refused message did not fit behind the unsent part of `accepted`.
    EXPECT_GT(accepted, event_server::max_queued_bytes - line_size);
    // The sent part is what the socket buffered for the client; nothing else
    // left the queue, so the queue itself was not allowed to grow further.
    EXPECT_LE(accepted,
              event_server::max_queued_bytes + socket_buffer_allowance());
    EXPECT_EQ(calls, call_list({"connected", "disconnected"}));
    EXPECT_TRUE(log.contains(log_level::warning, "outgoing queue"));
    // The client still gets what was already on its way, then end of stream.
    EXPECT_TRUE(wait([&] { return client.closed(); }));
    EXPECT_TRUE(client.received().starts_with(message + "\n"));

    // The server itself carries on with the next client.
    auto next = connect();
    server->queue_message("fresh");
    EXPECT_TRUE(wait([&] { return !next.lines().empty(); }));
    EXPECT_EQ(next.received(), "fresh\n");
}

TEST_F(EventServerTest, ShutdownDropsTheClientAndStopsListening) {
    start();
    auto client = connect();

    server->shutdown();

    EXPECT_EQ(calls, call_list({"connected", "disconnected"}));
    EXPECT_TRUE(wait([&] { return client.closed(); }));
    EXPECT_THROW(socket_client {sock_path()}, std::system_error);

    // Messages after shutdown go nowhere and do no harm.
    EXPECT_NO_THROW(server->queue_message("ignored"));
}

} // namespace
