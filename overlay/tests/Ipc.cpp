#include "Support.hpp"
#include "hyprcast/protocol/wire.hpp"
#include "ipc/ConnectionState.hpp"
#include "ipc/IpcClient.hpp"
#include "ipc/LineFramer.hpp"
#include "ipc/Protocol.hpp"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

using namespace hyprcast::overlay;
using tests::waitFor;

namespace {

ProtocolMessage parse(const QByteArray& json) {
    auto message = parseMessage(json);
    if (!message) {
        ADD_FAILURE() << "expected a valid protocol message: "
                      << message.error().toStdString();
        return {};
    }
    return std::move(*message);
}

// The line a plugin opens every connection with.
QByteArray hello(std::uint32_t version = hyprcast::wire::protocol_version) {
    return QByteArrayLiteral(R"({"event":"hello","protocol_version":)")
        + QByteArray::number(version) + QByteArrayLiteral("}\n");
}

TEST(LineFramerTest, SplitsFragmentedAndCoalescedLines) {
    LineFramer framer;

    auto lines = framer.append(QByteArrayLiteral("one\ntw"));
    ASSERT_TRUE(lines.has_value()) << "first fragmented append succeeds";
    ASSERT_EQ(*lines, QList<QByteArray> {QByteArrayLiteral("one")})
        << "first complete frame extracted";
    lines = framer.append(QByteArrayLiteral("o\r\nthree\nfour"));
    ASSERT_TRUE(lines.has_value()) << "second fragmented append succeeds";
    ASSERT_EQ(*lines,
              (QList<QByteArray> {
                  QByteArrayLiteral("two"),
                  QByteArrayLiteral("three")
              }))
        << "coalesced frames and CRLF parsed";
    lines = framer.append(QByteArrayLiteral("\n"));
    ASSERT_TRUE(lines.has_value()) << "final fragment completes";
    ASSERT_EQ(*lines, QList<QByteArray> {QByteArrayLiteral("four")})
        << "final frame extracted";
}

TEST(LineFramerTest, RejectsAnOversizedFrame) {
    LineFramer framer;
    const QByteArray oversized(LineFramer::MaxFrameBytes + 1, 'x');
    const auto lines = framer.append(oversized);
    ASSERT_FALSE(lines.has_value())
        << "oversized frame rejected, so no frames are applied";
    ASSERT_FALSE(lines.error().isEmpty()) << "oversize error reported";
}

TEST(ProtocolParsing, AcceptsEveryMessageKind) {
    const auto castingState = parseMessage(QByteArrayLiteral(
        "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":true}"));
    ASSERT_TRUE(castingState.has_value()) << "casting state accepted";
    ASSERT_TRUE(std::get<CastingStateMessage>(*castingState).paused)
        << "casting state parsed";

    const auto snapshot = parse(QByteArrayLiteral(
        "{\"event\":\"subscribe_keyboard\",\"keyboard_id\":7,\"name\":\"kbd\","
        "\"depressed\":1,\"latched\":2,\"locked\":3,\"group\":4,"
        "\"keymap\":\"xkb_keymap\",\"rate\":25,\"delay\":600}"));
    ASSERT_TRUE(std::get<KeyboardSnapshotMessage>(snapshot).id == 7)
        << "keyboard snapshot id parsed";
    ASSERT_TRUE(std::get<KeyboardSnapshotMessage>(snapshot).repeatInfo.delay
                == 600)
        << "keyboard repeat delay parsed";

    ASSERT_TRUE(std::holds_alternative<UnsubscribeKeyboardMessage>(
        parse(QByteArrayLiteral(
            "{\"event\":\"unsubscribe_keyboard\",\"keyboard_id\":7}"))))
        << "keyboard unsubscribe accepted";
    const auto key = parse(
        QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":7,\"time_ms\":12,"
                          "\"keycode\":30,\"state\":\"pressed\"}"));
    ASSERT_TRUE(std::get<KeyMessage>(key).pressed) << "key press parsed";
    ASSERT_TRUE(
        std::holds_alternative<ModifiersMessage>(parse(QByteArrayLiteral(
            "{\"event\":\"modifiers\",\"keyboard_id\":7,\"depressed\":1,"
            "\"latched\":2,\"locked\":3,\"group\":4}"))))
        << "modifier update accepted";
    ASSERT_TRUE(std::holds_alternative<KeymapMessage>(
        parse(QByteArrayLiteral("{\"event\":\"keymap\",\"keyboard_id\":7,"
                                "\"keymap\":\"new map\"}"))))
        << "keymap update accepted";
    ASSERT_TRUE(std::holds_alternative<RepeatInfoMessage>(
        parse(QByteArrayLiteral("{\"event\":\"repeat_info\",\"keyboard_"
                                "id\":7,\"rate\":25,\"delay\":600}"))))
        << "repeat update accepted";
}

TEST(ProtocolParsing, RejectsMalformedAndOutOfRangeMessages) {
    const std::array invalid {
        QByteArrayLiteral("not json"),
        QByteArrayLiteral("[]"),
        QByteArrayLiteral(
            "{\"event\":\"casting_state\",\"keyboard_id\":1,\"paused\":false}"),
        QByteArrayLiteral(
            "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":1}"),
        QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":-1,"
                          "\"keycode\":30,\"state\":\"pressed\"}"),
        QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":1."
                          "5,\"keycode\":30,\"state\":\"pressed\"}"),
        QByteArrayLiteral(
            "{\"event\":\"key\",\"keyboard_id\":4294967296,\"time_ms\":1,"
            "\"keycode\":30,\"state\":\"pressed\"}"),
        QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":0,"
                          "\"keycode\":30,\"state\":\"down\"}"),
        QByteArrayLiteral(
            "{\"event\":\"subscribe_keyboard\",\"keyboard_id\":1,\"name\":"
            "\"kbd\",\"depressed\":0,\"latched\":0,\"locked\":0,\"group\":"
            "0,\"keymap\":\"\",\"rate\":0,\"delay\":0}"),
        QByteArrayLiteral("{\"event\":\"repeat_info\",\"keyboard_id\":1,"
                          "\"rate\":-1,\"delay\":0}"),
        QByteArrayLiteral("{\"event\":\"future_event\",\"keyboard_id\":1}"),
        // The hello opens a connection and is not a message within one.
        hello(),
    };
    for (const auto& json : invalid) {
        const auto message = parseMessage(json);
        ASSERT_FALSE(message.has_value())
            << "malformed or out-of-range message rejected";
        ASSERT_TRUE(!message.has_value() && !message.error().isEmpty())
            << "parse failure has a diagnostic";
    }
}

TEST(ProtocolHello, MatchingVersionIsAccepted) {
    ASSERT_TRUE(checkHello(hello()).has_value()) << "matching version accepted";
}

TEST(ProtocolHello, OlderPluginIsToBeReloaded) {
    using hyprcast::wire::protocol_version;

    const auto older = checkHello(hello(protocol_version - 1));
    ASSERT_FALSE(older.has_value()) << "older plugin refused";
    ASSERT_TRUE(!older.has_value()
                && older.error().contains("Reload the plugin")
                && older.error().contains("restart Hyprland"))
        << "an older plugin is to be reloaded";
    ASSERT_TRUE(
        !older.has_value()
        && older.error().contains(QString::number(protocol_version - 1)))
        << "the plugin's version is named";
}

TEST(ProtocolHello, NewerPluginMeansAnOlderOverlay) {
    using hyprcast::wire::protocol_version;

    const auto newer = checkHello(hello(protocol_version + 1));
    ASSERT_FALSE(newer.has_value()) << "newer plugin refused";
    ASSERT_TRUE(!newer.has_value()
                && newer.error().contains("Restart the overlay"))
        << "an older overlay is to be restarted";
    ASSERT_TRUE(!newer.has_value()
                && newer.error().contains("end the running hyprcast-overlay"))
        << "by ending its process: the restart command keeps the program";
    ASSERT_TRUE(!newer.has_value()
                && newer.error().contains("hyprcast-overlay quit"))
        << "the advice names the command that ends it";
}

TEST(ProtocolHello, LineWithoutAVersionIsRefused) {
    // A plugin from before the hello existed starts with its capture state.
    const std::array withoutVersion {
        QByteArrayLiteral(
            "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":true}"),
        QByteArrayLiteral("{\"event\":\"hello\"}"),
        QByteArrayLiteral("{\"event\":\"hello\",\"protocol_version\":\"1\"}"),
        QByteArrayLiteral("{\"event\":\"hello\",\"protocol_version\":1.5}"),
        QByteArrayLiteral("{\"event\":\"hello\",\"protocol_version\":-1}"),
        QByteArrayLiteral("{\"protocol_version\":1}"),
        QByteArrayLiteral("not json"),
        QByteArrayLiteral(""),
    };
    for (const auto& line : withoutVersion) {
        const auto result = checkHello(line);
        ASSERT_FALSE(result.has_value())
            << "a line without a version is refused";
        ASSERT_TRUE(!result.has_value()
                    && result.error().contains("Reload the plugin"))
            << "a plugin without a version is to be reloaded";
    }
}

TEST(ConnectionStateTest, FollowsAKeyboardFromSubscriptionToRemoval) {
    ConnectionState state;
    ASSERT_TRUE(state
                    .apply(parse(QByteArrayLiteral(
                        "{\"event\":\"casting_state\",\"keyboard_"
                        "id\":0,\"paused\":true}")))
                    .has_value())
        << "pause state applied";
    ASSERT_TRUE(state.hasCastingState() && state.paused())
        << "pause state retained";
    ASSERT_TRUE(
        state
            .apply(parse(QByteArrayLiteral(
                "{\"event\":\"subscribe_keyboard\",\"keyboard_id\":1,"
                "\"name\":\"kbd\",\"depressed\":0,\"latched\":0,\"locked\":0,"
                "\"group\":0,"
                "\"keymap\":\"xkb\",\"rate\":30,\"delay\":500}")))
            .has_value())
        << "keyboard snapshot applied";
    ASSERT_EQ(state.keyboardCount(), 1) << "keyboard subscription stored";
    ASSERT_TRUE(
        state
            .apply(parse(QByteArrayLiteral(
                "{\"event\":\"modifiers\",\"keyboard_id\":1,\"depressed\":1,"
                "\"latched\":0,\"locked\":0,\"group\":0}")))
            .has_value())
        << "modifier update applied";
    ASSERT_TRUE(
        state
            .apply(parse(QByteArrayLiteral("{\"event\":\"keymap\",\"keyboard_"
                                           "id\":1,\"keymap\":\"updated\"}")))
            .has_value())
        << "keymap update applied";
    ASSERT_TRUE(state
                    .apply(parse(QByteArrayLiteral(
                        "{\"event\":\"repeat_info\",\"keyboard_"
                        "id\":1,\"rate\":60,\"delay\":700}")))
                    .has_value())
        << "repeat update applied";
    ASSERT_TRUE(state
                    .apply(parse(QByteArrayLiteral(
                        "{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":1,"
                        "\"keycode\":30,\"state\":\"pressed\"}")))
                    .has_value())
        << "key event accepted for subscribed keyboard";
    ASSERT_TRUE(
        state
            .apply(parse(QByteArrayLiteral(
                "{\"event\":\"unsubscribe_keyboard\",\"keyboard_id\":1}")))
            .has_value())
        << "keyboard unsubscribe applied";
    ASSERT_EQ(state.keyboardCount(), 0)
        << "keyboard unsubscription removes state";
    ASSERT_FALSE(state
                     .apply(parse(QByteArrayLiteral(
                         "{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":2,"
                         "\"keycode\":30,\"state\":\"released\"}")))
                     .has_value())
        << "events for unsubscribed keyboards rejected";

    state.reset();
    ASSERT_TRUE(!state.hasCastingState() && state.keyboardCount() == 0)
        << "reset clears connection-dependent state";
}

// The plugin withdraws every keyboard when input stops flowing and announces
// them again when it resumes; it also announces a keyboard that is already
// known. All of that has to be a valid sequence.
TEST(ConnectionStateTest, FollowsResentSnapshots) {
    const QByteArray subscribe = QByteArrayLiteral(
        R"({"event":"subscribe_keyboard","keyboard_id":1,"name":"kbd",)"
        R"("depressed":0,"latched":0,"locked":0,"group":0,"keymap":"xkb",)"
        R"("rate":30,"delay":500})");
    const QByteArray unsubscribe = QByteArrayLiteral(
        R"({"event":"unsubscribe_keyboard","keyboard_id":1})");
    const QByteArray press = QByteArrayLiteral(
        R"({"event":"key","keyboard_id":1,"time_ms":1,"keycode":30,)"
        R"("state":"pressed"})");
    const auto state = [](bool paused) {
        return paused
            ? QByteArrayLiteral(
                  R"({"event":"casting_state","keyboard_id":0,"paused":true})")
            : QByteArrayLiteral(
                  R"({"event":"casting_state","keyboard_id":0,"paused":false})");
    };

    ConnectionState connection;
    const auto accepts = [&connection](const QByteArray& line) {
        return connection.apply(parse(line)).has_value();
    };

    // Connected and paused: the state and no keyboards.
    ASSERT_TRUE(accepts(state(true)) && connection.keyboardCount() == 0)
        << "a paused connection knows no keyboard";
    ASSERT_FALSE(accepts(press))
        << "input for a keyboard never announced is rejected";

    ASSERT_TRUE(accepts(state(false)) && accepts(subscribe) && accepts(press))
        << "enabling announces the keyboard and input follows";
    ASSERT_TRUE(accepts(subscribe) && connection.keyboardCount() == 1
                && accepts(press))
        << "a repeated announcement replaces the keyboard";

    ASSERT_TRUE(accepts(state(true)) && accepts(unsubscribe)
                && connection.keyboardCount() == 0)
        << "pausing withdraws the keyboard";
    ASSERT_FALSE(accepts(press))
        << "input for a withdrawn keyboard is rejected";
    ASSERT_TRUE(accepts(state(false)) && accepts(subscribe) && accepts(press))
        << "enabling again announces the keyboard again";

    ASSERT_TRUE(accepts(unsubscribe) && !accepts(press))
        << "a lock withdraws the keyboard while capture stays enabled";
    ASSERT_TRUE(accepts(subscribe) && accepts(press))
        << "an unlock announces the keyboard again";
}

TEST(IpcClientTest, RetriesUntilTheServerAppears) {
    const QTemporaryDir tempDirectory;
    ASSERT_TRUE(tempDirectory.isValid())
        << "retry test temporary directory created";
    const QString socketPath =
        tempDirectory.filePath(QStringLiteral("late-events.sock"));
    QLocalServer::removeServer(socketPath);

    IpcClient client(socketPath);
    int failedAttempts = 0;
    QObject::connect(
        &client,
        &IpcClient::connectionReset,
        &client,
        [&] { ++failedAttempts; },
        Qt::DirectConnection);
    client.start();
    ASSERT_TRUE(waitFor([&] {
        return failedAttempts > 0 && !client.connected();
    })) << "failed connection attempt is abandoned before the server exists";

    QLocalServer server;
    ASSERT_TRUE(server.listen(socketPath)) << "late test IPC server listens";
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto* peer = server.nextPendingConnection();
            peer->write(
                hello()
                + QByteArrayLiteral("{\"event\":\"casting_state\",\"keyboard_"
                                    "id\":0,\"paused\":false}\n"));
            peer->flush();
        }
    });
    ASSERT_TRUE(waitFor([&] {
        return client.connected() && client.hasCastingState();
    })) << "client connects when the server appears after a refused attempt";
}

TEST(IpcClientTest, ReconnectsAndReplacesStaleState) {
    const QTemporaryDir tempDirectory;
    ASSERT_TRUE(tempDirectory.isValid()) << "temporary directory created";
    const QString socketPath =
        tempDirectory.filePath(QStringLiteral("events.sock"));
    QLocalServer::removeServer(socketPath);

    QLocalServer server;
    ASSERT_TRUE(server.listen(socketPath)) << "test IPC server listens";

    QPointer<QLocalSocket> firstPeer;
    int connectionCount = 0;
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto* peer = server.nextPendingConnection();
            ++connectionCount;
            if (connectionCount == 1) {
                firstPeer = peer;
                peer->write(hello());
                peer->write(QByteArrayLiteral(
                    "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":"
                    "true}\n"
                    "{\"event\":\"subscribe_keyboard\",\"keyboard_id\":1,"
                    "\"name\":\"test "
                    "keyboard\",\"depressed\":0,\"latched\":0,\"locked\":0,"
                    "\"group\":0,\"keymap\":\"xkb\",\"rate\":25,\"delay\":600}"
                    "\n"
                    "{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":1,"
                    "\"keycode\":30,\"state\":\"pressed\"}\n"));
                peer->flush();
            } else if (connectionCount == 2) {
                peer->write(hello());
                peer->write(QByteArrayLiteral(
                    "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":"
                    "false}\n"
                    "{\"event\":\"subscribe_keyboard\",\"keyboard_id\":2,"
                    "\"name\":\"reconnected "
                    "keyboard\",\"depressed\":0,\"latched\":0,\"locked\":0,"
                    "\"group\":0,\"keymap\":\"xkb\",\"rate\":30,\"delay\":500}"
                    "\n"));
                peer->flush();
            }
        }
    });

    IpcClient client(socketPath);
    std::vector<int> deliveredEvents;
    int resetNotifications = 0;
    QObject::connect(
        &client,
        &IpcClient::protocolMessageReceived,
        &client,
        [&](const ProtocolMessage& message) {
            deliveredEvents.push_back(static_cast<int>(message.index()));
            if (std::holds_alternative<KeyMessage>(message)) {
                // Not ASSERT: the event loop calls this.
                EXPECT_EQ(client.keyboardCount(), 1)
                    << "message is delivered after its connection-state "
                       "transition";
            }
        },
        Qt::DirectConnection);
    QObject::connect(
        &client,
        &IpcClient::connectionReset,
        &client,
        [&] { ++resetNotifications; },
        Qt::DirectConnection);
    client.start();
    ASSERT_TRUE(waitFor([&] {
        return client.connected() && client.hasCastingState()
            && client.keyboardCount() == 1 && deliveredEvents.size() == 3;
    })) << "async client connects and consumes coalesced state messages";
    ASSERT_TRUE((deliveredEvents == std::vector<int> {0, 1, 3}))
        << "validated protocol messages are delivered in wire order";
    ASSERT_TRUE(client.paused()) << "casting pause state received over socket";
    ASSERT_FALSE(firstPeer.isNull()) << "first server-side connection accepted";

    firstPeer->abort();
    ASSERT_TRUE(waitFor([&] {
        return !client.connected() && !client.hasCastingState()
            && client.keyboardCount() == 0;
    })) << "disconnect clears connection state";
    ASSERT_TRUE(resetNotifications > 0)
        << "disconnect explicitly notifies consumers to reset held input state";
    ASSERT_TRUE(waitFor([&] {
        return connectionCount >= 2 && client.connected()
            && client.hasCastingState() && client.keyboardCount() == 1;
    })) << "client reconnects after bounded delay and accepts a fresh snapshot";
    ASSERT_FALSE(client.paused())
        << "reconnected casting snapshot replaces stale pause state";
    ASSERT_TRUE((deliveredEvents == std::vector<int> {0, 1, 3, 0, 1}))
        << "reconnected messages continue in wire order";

    server.close();
    QLocalServer::removeServer(socketPath);
}

TEST(IpcClientTest, RejectedTransitionIsNotDelivered) {
    const QTemporaryDir tempDirectory;
    ASSERT_TRUE(tempDirectory.isValid())
        << "invalid-transition temporary directory created";
    const QString socketPath =
        tempDirectory.filePath(QStringLiteral("invalid-transition.sock"));
    QLocalServer::removeServer(socketPath);

    QLocalServer server;
    ASSERT_TRUE(server.listen(socketPath))
        << "invalid-transition test server listens";
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto* peer = server.nextPendingConnection();
            peer->write(hello());
            peer->write(QByteArrayLiteral(
                "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":"
                "false}\n"
                "{\"event\":\"key\",\"keyboard_id\":7,\"time_ms\":1,"
                "\"keycode\":30,\"state\":\"pressed\"}\n"));
            peer->flush();
        }
    });

    IpcClient client(socketPath);
    std::vector<int> deliveredEvents;
    bool rejected = false;
    QObject::connect(
        &client,
        &IpcClient::protocolMessageReceived,
        &client,
        [&](const ProtocolMessage& message) {
            deliveredEvents.push_back(static_cast<int>(message.index()));
        },
        Qt::DirectConnection);
    // The valid first message is delivered; the reset that follows it is the
    // rejection.
    QObject::connect(
        &client,
        &IpcClient::connectionReset,
        &client,
        [&] { rejected = rejected || !deliveredEvents.empty(); },
        Qt::DirectConnection);
    client.start();
    ASSERT_TRUE(waitFor([&] { return rejected; }))
        << "invalid connection-state transition is rejected";
    ASSERT_TRUE(deliveredEvents == std::vector<int> {0})
        << "rejected transition never reaches the keyboard interpreter";

    server.close();
    QLocalServer::removeServer(socketPath);
}

// A plugin that does not open with this overlay's protocol version is hung up
// on before anything it sends is believed, so capture cannot be requested.
void expectRefused(const QByteArray& greeting, const char* advice) {
    const QTemporaryDir tempDirectory;
    ASSERT_TRUE(tempDirectory.isValid()) << "temporary directory created";
    const QString socketPath =
        tempDirectory.filePath(QStringLiteral("events.sock"));

    QLocalServer server;
    ASSERT_TRUE(server.listen(socketPath)) << "incompatible plugin listens";
    std::vector<std::unique_ptr<QLocalSocket>> peers;
    QByteArray received;
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto& peer = peers.emplace_back(server.nextPendingConnection());
            QObject::connect(peer.get(),
                             &QLocalSocket::readyRead,
                             peer.get(),
                             [&received, socket = peer.get()] {
                                 received += socket->readAll();
                             });
            // Everything a plugin could say to get capture going.
            peer->write(greeting);
            peer->write(QByteArrayLiteral(
                "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":"
                "false}\n"
                "{\"event\":\"subscribe_keyboard\",\"keyboard_id\":1,"
                "\"name\":\"kbd\",\"depressed\":0,\"latched\":0,"
                "\"locked\":0,\"group\":0,\"keymap\":\"xkb\",\"rate\":25,"
                "\"delay\":600}\n"
                "{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":1,"
                "\"keycode\":30,\"state\":\"pressed\"}\n"));
            peer->flush();
        }
    });

    IpcClient client(socketPath);
    int delivered = 0;
    QObject::connect(
        &client,
        &IpcClient::protocolMessageReceived,
        &client,
        [&] { ++delivered; },
        Qt::DirectConnection);
    client.start();

    // Capture can be requested only once a capture state is known.
    bool believed = false;
    ASSERT_TRUE(waitFor([&] {
        believed = believed || client.hasCastingState();
        return !client.incompatibility().isEmpty();
    })) << "the incompatible plugin is recognised";
    ASSERT_TRUE(client.incompatibility().contains(advice))
        << "the refusal says what to restart";
    ASSERT_TRUE(waitFor([&] {
        believed = believed || client.hasCastingState();
        return !peers.empty()
            && peers.front()->state() == QLocalSocket::UnconnectedState;
    })) << "the overlay hangs up on an incompatible plugin";
    ASSERT_TRUE(!believed && client.keyboardCount() == 0)
        << "nothing an incompatible plugin says is believed";
    ASSERT_EQ(delivered, 0) << "nothing an incompatible plugin says is shown";
    ASSERT_FALSE(client.setCaptureEnabled(true))
        << "capture cannot be requested from an incompatible plugin";
    ASSERT_TRUE(received.isEmpty())
        << "an incompatible plugin is sent no command";

    server.close();
    QLocalServer::removeServer(socketPath);
}

TEST(IncompatiblePlugin, FromBeforeTheHelloIsRefused) {
    expectRefused({}, "Reload the plugin");
}

TEST(IncompatiblePlugin, OlderIsRefused) {
    expectRefused(hello(hyprcast::wire::protocol_version - 1),
                  "Reload the plugin");
}

TEST(IncompatiblePlugin, NewerIsRefused) {
    expectRefused(hello(hyprcast::wire::protocol_version + 1),
                  "Restart the overlay");
}

// The overlay keeps trying, so a reloaded plugin is picked up. Until then the
// reason stays, so that a toggle between two attempts can report it.
TEST(IncompatiblePlugin, RefusalEndsWithThePlugin) {
    using hyprcast::wire::protocol_version;

    const QTemporaryDir tempDirectory;
    ASSERT_TRUE(tempDirectory.isValid()) << "temporary directory created";
    const QString socketPath =
        tempDirectory.filePath(QStringLiteral("events.sock"));

    QLocalServer server;
    ASSERT_TRUE(server.listen(socketPath)) << "plugin listens";
    QByteArray greeting = hello(protocol_version - 1);
    QPointer<QLocalSocket> peer;
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            peer = server.nextPendingConnection();
            peer->write(greeting);
            peer->write(
                QByteArrayLiteral("{\"event\":\"casting_state\",\"keyboard_"
                                  "id\":0,\"paused\":true}\n"));
            peer->flush();
        }
    });

    IpcClient client(socketPath);
    client.start();
    ASSERT_TRUE(waitFor([&] { return !client.incompatibility().isEmpty(); }))
        << "an older plugin is refused";

    // The plugin is reloaded: the next attempt meets a compatible one.
    greeting = hello();
    ASSERT_TRUE(waitFor([&] {
        return client.connected() && client.hasCastingState();
    })) << "a reloaded plugin is accepted without restarting the overlay";
    ASSERT_TRUE(client.incompatibility().isEmpty())
        << "a compatible plugin ends the incompatibility";

    // A newer plugin replaces it.
    greeting = hello(protocol_version + 1);
    ASSERT_FALSE(peer.isNull()) << "compatible connection accepted";
    peer->abort();
    ASSERT_TRUE(waitFor([&] { return !client.incompatibility().isEmpty(); }))
        << "a newer plugin is refused";

    // The plugin is unloaded: nothing is left to be incompatible with.
    server.close();
    QLocalServer::removeServer(socketPath);
    ASSERT_TRUE(waitFor([&] { return client.incompatibility().isEmpty(); }))
        << "an unloaded plugin is not reported as incompatible";
    ASSERT_FALSE(client.connected()) << "no plugin is connected";
}

} // namespace

int main(int argc, char** argv) {
    return tests::run<QCoreApplication>(argc, argv);
}
