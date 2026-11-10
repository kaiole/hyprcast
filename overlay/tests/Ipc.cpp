#include "hyprcast/protocol/wire.hpp"
#include "ipc/ConnectionState.hpp"
#include "ipc/IpcClient.hpp"
#include "ipc/LineFramer.hpp"
#include "ipc/Protocol.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <variant>
#include <vector>

using namespace hyprcast::overlay;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "check failed: " << message << '\n';
        std::_Exit(1);
    }
}

ProtocolMessage parse(const QByteArray& json) {
    const auto message = parseMessage(json);
    check(message.has_value(), "expected valid protocol message");
    return message.value_or(ProtocolMessage {});
}

// The line a plugin opens every connection with.
QByteArray hello(std::uint32_t version = hyprcast::wire::protocol_version) {
    return QByteArrayLiteral(R"({"event":"hello","protocol_version":)")
        + QByteArray::number(version) + QByteArrayLiteral("}\n");
}

void testLineFraming() {
    LineFramer framer;

    auto lines = framer.append(QByteArrayLiteral("one\ntw"));
    check(lines.has_value(), "first fragmented append succeeds");
    check(lines == QList<QByteArray> {QByteArrayLiteral("one")},
          "first complete frame extracted");
    lines = framer.append(QByteArrayLiteral("o\r\nthree\nfour"));
    check(lines.has_value(), "second fragmented append succeeds");
    check(
        lines
            == QList<
                QByteArray> {QByteArrayLiteral("two"), QByteArrayLiteral("three")},
        "coalesced frames and CRLF parsed");
    lines = framer.append(QByteArrayLiteral("\n"));
    check(lines.has_value(), "final fragment completes");
    check(lines == QList<QByteArray> {QByteArrayLiteral("four")},
          "final frame extracted");

    const QByteArray oversized(LineFramer::MaxFrameBytes + 1, 'x');
    lines = framer.append(oversized);
    check(!lines.has_value(),
          "oversized frame rejected, so no frames are applied");
    check(!lines.error().isEmpty(), "oversize error reported");
}

void testMessageParsing() {
    const auto castingState = parseMessage(QByteArrayLiteral(
        "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":true}"));
    check(castingState.has_value(), "casting state accepted");
    check(std::get<CastingStateMessage>(*castingState).paused,
          "casting state parsed");

    const auto snapshot = parse(QByteArrayLiteral(
        "{\"event\":\"subscribe_keyboard\",\"keyboard_id\":7,\"name\":\"kbd\","
        "\"depressed\":1,\"latched\":2,\"locked\":3,\"group\":4,"
        "\"keymap\":\"xkb_keymap\",\"rate\":25,\"delay\":600}"));
    check(std::get<KeyboardSnapshotMessage>(snapshot).id == 7,
          "keyboard snapshot id parsed");
    check(std::get<KeyboardSnapshotMessage>(snapshot).repeatInfo.delay == 600,
          "keyboard repeat delay parsed");

    check(std::holds_alternative<UnsubscribeKeyboardMessage>(
              parse(QByteArrayLiteral(
                  "{\"event\":\"unsubscribe_keyboard\",\"keyboard_id\":7}"))),
          "keyboard unsubscribe accepted");
    const auto key = parse(
        QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":7,\"time_ms\":12,"
                          "\"keycode\":30,\"state\":\"pressed\"}"));
    check(std::get<KeyMessage>(key).pressed, "key press parsed");
    check(std::holds_alternative<ModifiersMessage>(parse(QByteArrayLiteral(
              "{\"event\":\"modifiers\",\"keyboard_id\":7,\"depressed\":1,"
              "\"latched\":2,\"locked\":3,\"group\":4}"))),
          "modifier update accepted");
    check(std::holds_alternative<KeymapMessage>(
              parse(QByteArrayLiteral("{\"event\":\"keymap\",\"keyboard_id\":7,"
                                      "\"keymap\":\"new map\"}"))),
          "keymap update accepted");
    check(std::holds_alternative<RepeatInfoMessage>(
              parse(QByteArrayLiteral("{\"event\":\"repeat_info\",\"keyboard_"
                                      "id\":7,\"rate\":25,\"delay\":600}"))),
          "repeat update accepted");

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
        check(!message.has_value(),
              "malformed or out-of-range message rejected");
        check(!message.has_value() && !message.error().isEmpty(),
              "parse failure has a diagnostic");
    }
}

void testHello() {
    using hyprcast::wire::protocol_version;

    check(checkHello(hello()).has_value(), "matching version accepted");

    const auto older = checkHello(hello(protocol_version - 1));
    check(!older.has_value(), "older plugin refused");
    check(!older.has_value() && older.error().contains("Reload the plugin")
              && older.error().contains("restart Hyprland"),
          "an older plugin is to be reloaded");
    check(!older.has_value()
              && older.error().contains(QString::number(protocol_version - 1)),
          "the plugin's version is named");

    const auto newer = checkHello(hello(protocol_version + 1));
    check(!newer.has_value(), "newer plugin refused");
    check(!newer.has_value() && newer.error().contains("Restart the overlay"),
          "an older overlay is to be restarted");

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
        check(!result.has_value(), "a line without a version is refused");
        check(!result.has_value()
                  && result.error().contains("Reload the plugin"),
              "a plugin without a version is to be reloaded");
    }
}

void testConnectionState() {
    ConnectionState state;
    check(state
              .apply(parse(
                  QByteArrayLiteral("{\"event\":\"casting_state\",\"keyboard_"
                                    "id\":0,\"paused\":true}")))
              .has_value(),
          "pause state applied");
    check(state.hasCastingState() && state.paused(), "pause state retained");
    check(state
              .apply(parse(QByteArrayLiteral(
                  "{\"event\":\"subscribe_keyboard\",\"keyboard_id\":1,"
                  "\"name\":\"kbd\",\"depressed\":0,\"latched\":0,\"locked\":0,"
                  "\"group\":0,"
                  "\"keymap\":\"xkb\",\"rate\":30,\"delay\":500}")))
              .has_value(),
          "keyboard snapshot applied");
    check(state.keyboardCount() == 1, "keyboard subscription stored");
    check(state
              .apply(parse(QByteArrayLiteral(
                  "{\"event\":\"modifiers\",\"keyboard_id\":1,\"depressed\":1,"
                  "\"latched\":0,\"locked\":0,\"group\":0}")))
              .has_value(),
          "modifier update applied");
    check(state
              .apply(parse(QByteArrayLiteral("{\"event\":\"keymap\",\"keyboard_"
                                             "id\":1,\"keymap\":\"updated\"}")))
              .has_value(),
          "keymap update applied");
    check(state
              .apply(parse(
                  QByteArrayLiteral("{\"event\":\"repeat_info\",\"keyboard_"
                                    "id\":1,\"rate\":60,\"delay\":700}")))
              .has_value(),
          "repeat update applied");
    check(state
              .apply(parse(QByteArrayLiteral(
                  "{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":1,"
                  "\"keycode\":30,\"state\":\"pressed\"}")))
              .has_value(),
          "key event accepted for subscribed keyboard");
    check(state
              .apply(parse(QByteArrayLiteral(
                  "{\"event\":\"unsubscribe_keyboard\",\"keyboard_id\":1}")))
              .has_value(),
          "keyboard unsubscribe applied");
    check(state.keyboardCount() == 0, "keyboard unsubscription removes state");
    check(!state
               .apply(parse(QByteArrayLiteral(
                   "{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":2,"
                   "\"keycode\":30,\"state\":\"released\"}")))
               .has_value(),
          "events for unsubscribed keyboards rejected");

    state.reset();
    check(!state.hasCastingState() && state.keyboardCount() == 0,
          "reset clears connection-dependent state");
}

template <typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer::singleShot(2, &loop, &QEventLoop::quit);
        loop.exec();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

void testRetryUntilServerAppears() {
    const QTemporaryDir tempDirectory;
    check(tempDirectory.isValid(), "retry test temporary directory created");
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
    check(waitUntil([&] { return failedAttempts > 0 && !client.connected(); },
                    1000),
          "failed connection attempt is abandoned before the server exists");

    QLocalServer server;
    check(server.listen(socketPath), "late test IPC server listens");
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
    check(waitUntil(
              [&] { return client.connected() && client.hasCastingState(); },
              2000),
          "client connects when the server appears after a refused attempt");
}

void testAsyncReconnect() {
    const QTemporaryDir tempDirectory;
    check(tempDirectory.isValid(), "temporary directory created");
    const QString socketPath =
        tempDirectory.filePath(QStringLiteral("events.sock"));
    QLocalServer::removeServer(socketPath);

    QLocalServer server;
    check(server.listen(socketPath), "test IPC server listens");

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
                check(client.keyboardCount() == 1,
                      "message is delivered after its connection-state "
                      "transition");
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
    check(waitUntil(
              [&] {
                  return client.connected() && client.hasCastingState()
                      && client.keyboardCount() == 1
                      && deliveredEvents.size() == 3;
              },
              2000),
          "async client connects and consumes coalesced state messages");
    check(deliveredEvents == std::vector<int> {0, 1, 3},
          "validated protocol messages are delivered in wire order");
    check(client.paused(), "casting pause state received over socket");
    check(!firstPeer.isNull(), "first server-side connection accepted");

    firstPeer->abort();
    check(waitUntil(
              [&] {
                  return !client.connected() && !client.hasCastingState()
                      && client.keyboardCount() == 0;
              },
              1000),
          "disconnect clears connection state");
    check(resetNotifications > 0,
          "disconnect explicitly notifies consumers to reset held input state");
    check(waitUntil(
              [&] {
                  return connectionCount >= 2 && client.connected()
                      && client.hasCastingState()
                      && client.keyboardCount() == 1;
              },
              3000),
          "client reconnects after bounded delay and accepts a fresh snapshot");
    check(!client.paused(),
          "reconnected casting snapshot replaces stale pause state");
    check(deliveredEvents == std::vector<int> {0, 1, 3, 0, 1},
          "reconnected messages continue in wire order");

    server.close();
    QLocalServer::removeServer(socketPath);
}

void testRejectedTransitionIsNotDelivered() {
    const QTemporaryDir tempDirectory;
    check(tempDirectory.isValid(),
          "invalid-transition temporary directory created");
    const QString socketPath =
        tempDirectory.filePath(QStringLiteral("invalid-transition.sock"));
    QLocalServer::removeServer(socketPath);

    QLocalServer server;
    check(server.listen(socketPath), "invalid-transition test server listens");
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
    check(waitUntil([&] { return rejected; }, 1000),
          "invalid connection-state transition is rejected");
    check(deliveredEvents == std::vector<int> {0},
          "rejected transition never reaches the keyboard interpreter");

    server.close();
    QLocalServer::removeServer(socketPath);
}

// A plugin that does not open with this overlay's protocol version is hung up
// on before anything it sends is believed, so capture cannot be requested.
void testIncompatiblePluginIsRefused() {
    using hyprcast::wire::protocol_version;

    struct Case {
        const char* name = "";
        QByteArray greeting {};
        const char* advice = "";
    };
    const std::array cases {
        Case {
            .name = "a plugin from before the hello",
            .greeting = {},
            .advice = "Reload the plugin",
        },
        Case {
            .name = "an older plugin",
            .greeting = hello(protocol_version - 1),
            .advice = "Reload the plugin",
        },
        Case {
            .name = "a newer plugin",
            .greeting = hello(protocol_version + 1),
            .advice = "Restart the overlay",
        },
    };

    for (const auto& incompatible : cases) {
        const QTemporaryDir tempDirectory;
        check(tempDirectory.isValid(), "temporary directory created");
        const QString socketPath =
            tempDirectory.filePath(QStringLiteral("events.sock"));

        QLocalServer server;
        check(server.listen(socketPath), "incompatible plugin listens");
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
                peer->write(incompatible.greeting);
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
        check(waitUntil(
                  [&] {
                      believed = believed || client.hasCastingState();
                      return !client.incompatibility().isEmpty();
                  },
                  2000),
              incompatible.name);
        check(client.incompatibility().contains(incompatible.advice),
              "the refusal says what to restart");
        check(waitUntil(
                  [&] {
                      believed = believed || client.hasCastingState();
                      return !peers.empty()
                          && peers.front()->state()
                          == QLocalSocket::UnconnectedState;
                  },
                  2000),
              "the overlay hangs up on an incompatible plugin");
        check(!believed && client.keyboardCount() == 0,
              "nothing an incompatible plugin says is believed");
        check(delivered == 0, "nothing an incompatible plugin says is shown");
        check(!client.setCaptureEnabled(true),
              "capture cannot be requested from an incompatible plugin");
        check(received.isEmpty(), "an incompatible plugin is sent no command");

        server.close();
        QLocalServer::removeServer(socketPath);
    }
}

// The overlay keeps trying, so a reloaded plugin is picked up. Until then the
// reason stays, so that a toggle between two attempts can report it.
void testIncompatibilityEndsWithTheIncompatiblePlugin() {
    using hyprcast::wire::protocol_version;

    const QTemporaryDir tempDirectory;
    check(tempDirectory.isValid(), "temporary directory created");
    const QString socketPath =
        tempDirectory.filePath(QStringLiteral("events.sock"));

    QLocalServer server;
    check(server.listen(socketPath), "plugin listens");
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
    check(waitUntil([&] { return !client.incompatibility().isEmpty(); }, 2000),
          "an older plugin is refused");

    // The plugin is reloaded: the next attempt meets a compatible one.
    greeting = hello();
    check(waitUntil(
              [&] { return client.connected() && client.hasCastingState(); },
              3000),
          "a reloaded plugin is accepted without restarting the overlay");
    check(client.incompatibility().isEmpty(),
          "a compatible plugin ends the incompatibility");

    // A newer plugin replaces it.
    greeting = hello(protocol_version + 1);
    check(!peer.isNull(), "compatible connection accepted");
    peer->abort();
    check(waitUntil([&] { return !client.incompatibility().isEmpty(); }, 3000),
          "a newer plugin is refused");

    // The plugin is unloaded: nothing is left to be incompatible with.
    server.close();
    QLocalServer::removeServer(socketPath);
    check(waitUntil([&] { return client.incompatibility().isEmpty(); }, 5000),
          "an unloaded plugin is not reported as incompatible");
    check(!client.connected(), "no plugin is connected");
}

} // namespace

int main(int argc, char* argv[]) {
    const QCoreApplication application(argc, argv);
    try {
        testLineFraming();
        testMessageParsing();
        testHello();
        testConnectionState();
        testRetryUntilServerAppears();
        testAsyncReconnect();
        testRejectedTransitionIsNotDelivered();
        testIncompatiblePluginIsRefused();
        testIncompatibilityEndsWithTheIncompatiblePlugin();
        std::cout << "overlay IPC tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        // std::get on a message of the wrong type throws.
        std::cerr << "check failed: " << error.what() << '\n';
        return 1;
    }
}
