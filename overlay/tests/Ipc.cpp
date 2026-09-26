#include "ipc/ConnectionState.hpp"
#include "ipc/IpcClient.hpp"
#include "ipc/LineFramer.hpp"
#include "ipc/Protocol.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

using namespace Hyprcast::Overlay;

namespace {
    void check(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "check failed: " << message << '\n';
            std::exit(1);
        }
    }

    ProtocolMessage parse(const QByteArray& json) {
        ProtocolMessage message;
        QString         error;
        check(parseMessage(json, &message, &error), "expected valid protocol message");
        return message;
    }

    void testLineFraming() {
        LineFramer        framer;
        QList<QByteArray> lines;
        QString           error;

        check(framer.append(QByteArrayLiteral("one\ntw"), &lines, &error), "first fragmented append succeeds");
        check(lines == QList<QByteArray>{QByteArrayLiteral("one")}, "first complete frame extracted");
        check(framer.append(QByteArrayLiteral("o\r\nthree\nfour"), &lines, &error), "second fragmented append succeeds");
        check(lines == QList<QByteArray>{QByteArrayLiteral("two"), QByteArrayLiteral("three")}, "coalesced frames and CRLF parsed");
        check(framer.append(QByteArrayLiteral("\n"), &lines, &error), "final fragment completes");
        check(lines == QList<QByteArray>{QByteArrayLiteral("four")}, "final frame extracted");

        const QByteArray oversized(LineFramer::MaxFrameBytes + 1, 'x');
        check(!framer.append(oversized, &lines, &error), "oversized frame rejected");
        check(lines.isEmpty(), "no frames applied after oversized input");
        check(!error.isEmpty(), "oversize error reported");
    }

    void testMessageParsing() {
        ProtocolMessage message;
        QString         error;
        check(parseMessage(QByteArrayLiteral("{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":true}"), &message, &error), "casting state accepted");
        check(std::get<CastingStateMessage>(message).paused, "casting state parsed");

        const auto snapshot = parse(QByteArrayLiteral("{\"event\":\"subscribe_keyboard\",\"keyboard_id\":7,\"name\":\"kbd\",\"depressed\":1,\"latched\":2,\"locked\":3,\"group\":4,"
                                                      "\"keymap\":\"xkb_keymap\",\"rate\":25,\"delay\":600}"));
        check(std::get<KeyboardSnapshotMessage>(snapshot).id == 7, "keyboard snapshot id parsed");
        check(std::get<KeyboardSnapshotMessage>(snapshot).repeatDelay == 600, "keyboard repeat delay parsed");

        check(std::holds_alternative<UnsubscribeKeyboardMessage>(parse(QByteArrayLiteral("{\"event\":\"unsubscribe_keyboard\",\"keyboard_id\":7}"))),
              "keyboard unsubscribe accepted");
        const auto key = parse(QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":7,\"time_ms\":12,\"keycode\":30,\"state\":\"pressed\"}"));
        check(std::get<KeyMessage>(key).pressed, "key press parsed");
        check(std::holds_alternative<ModifiersMessage>(
                  parse(QByteArrayLiteral("{\"event\":\"modifiers\",\"keyboard_id\":7,\"depressed\":1,\"latched\":2,\"locked\":3,\"group\":4}"))),
              "modifier update accepted");
        check(std::holds_alternative<KeymapMessage>(parse(QByteArrayLiteral("{\"event\":\"keymap\",\"keyboard_id\":7,\"keymap\":\"new map\"}"))), "keymap update accepted");
        check(std::holds_alternative<RepeatInfoMessage>(parse(QByteArrayLiteral("{\"event\":\"repeat_info\",\"keyboard_id\":7,\"rate\":25,\"delay\":600}"))),
              "repeat update accepted");

        const QByteArray invalid[] = {QByteArrayLiteral("not json"),
                                      QByteArrayLiteral("[]"),
                                      QByteArrayLiteral("{\"event\":\"casting_state\",\"keyboard_id\":1,\"paused\":false}"),
                                      QByteArrayLiteral("{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":1}"),
                                      QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":-1,\"keycode\":30,\"state\":\"pressed\"}"),
                                      QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":1.5,\"keycode\":30,\"state\":\"pressed\"}"),
                                      QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":4294967296,\"time_ms\":1,\"keycode\":30,\"state\":\"pressed\"}"),
                                      QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":0,\"keycode\":30,\"state\":\"down\"}"),
                                      QByteArrayLiteral("{\"event\":\"subscribe_keyboard\",\"keyboard_id\":1,\"name\":\"kbd\",\"depressed\":0,\"latched\":0,\"locked\":0,\"group\":"
                                                        "0,\"keymap\":\"\",\"rate\":0,\"delay\":0}"),
                                      QByteArrayLiteral("{\"event\":\"repeat_info\",\"keyboard_id\":1,\"rate\":-1,\"delay\":0}"),
                                      QByteArrayLiteral("{\"event\":\"future_event\",\"keyboard_id\":1}")};
        for (const auto& json : invalid) {
            check(!parseMessage(json, &message, &error), "malformed or out-of-range message rejected");
            check(!error.isEmpty(), "parse failure has a diagnostic");
        }
    }

    void testConnectionState() {
        ConnectionState state;
        QString         error;
        check(state.apply(parse(QByteArrayLiteral("{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":true}")), &error), "pause state applied");
        check(state.hasCastingState() && state.paused(), "pause state retained");
        check(state.apply(parse(QByteArrayLiteral("{\"event\":\"subscribe_keyboard\",\"keyboard_id\":1,\"name\":\"kbd\",\"depressed\":0,\"latched\":0,\"locked\":0,\"group\":0,"
                                                  "\"keymap\":\"xkb\",\"rate\":30,\"delay\":500}")),
                          &error),
              "keyboard snapshot applied");
        check(state.keyboards().size() == 1, "keyboard subscription stored");
        check(state.apply(parse(QByteArrayLiteral("{\"event\":\"modifiers\",\"keyboard_id\":1,\"depressed\":1,\"latched\":0,\"locked\":0,\"group\":0}")), &error),
              "modifier update applied");
        check(state.keyboards().value(1).depressed == 1, "modifier state updated");
        check(state.apply(parse(QByteArrayLiteral("{\"event\":\"keymap\",\"keyboard_id\":1,\"keymap\":\"updated\"}")), &error), "keymap update applied");
        check(state.keyboards().value(1).keymap == QStringLiteral("updated"), "keymap state updated");
        check(state.apply(parse(QByteArrayLiteral("{\"event\":\"repeat_info\",\"keyboard_id\":1,\"rate\":60,\"delay\":700}")), &error), "repeat update applied");
        check(state.keyboards().value(1).repeatRate == 60, "repeat state updated");
        check(state.apply(parse(QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":1,\"keycode\":30,\"state\":\"pressed\"}")), &error),
              "key event accepted for subscribed keyboard");
        check(state.lastEventSummary().contains(QStringLiteral("pressed")), "key event summarized for diagnostics");
        check(state.apply(parse(QByteArrayLiteral("{\"event\":\"unsubscribe_keyboard\",\"keyboard_id\":1}")), &error), "keyboard unsubscribe applied");
        check(state.keyboards().isEmpty(), "keyboard unsubscription removes state");
        check(!state.apply(parse(QByteArrayLiteral("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":2,\"keycode\":30,\"state\":\"released\"}")), &error),
              "events for unsubscribed keyboards rejected");

        state.reset();
        check(!state.hasCastingState() && state.keyboards().isEmpty() && state.lastEventSummary().isEmpty(), "reset clears connection-dependent state");
    }

    template <typename Predicate>
    bool waitUntil(Predicate predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(2);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        return predicate();
    }

    void testRetryUntilServerAppears() {
        QTemporaryDir tempDirectory;
        check(tempDirectory.isValid(), "retry test temporary directory created");
        const QString socketPath = tempDirectory.filePath(QStringLiteral("late-events.sock"));
        QLocalServer::removeServer(socketPath);

        IpcClient client(socketPath);
        client.start();
        check(waitUntil([&] { return client.statusText() == QStringLiteral("Reconnecting…"); }, 1000), "failed connection schedules an asynchronous retry");

        QLocalServer server;
        check(server.listen(socketPath), "late test IPC server listens");
        QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto* peer = server.nextPendingConnection();
                peer->write(QByteArrayLiteral("{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":false}\n"));
                peer->flush();
            }
        });
        check(waitUntil([&] { return client.connected() && client.hasCastingState(); }, 2000), "client connects when the server appears after a refused attempt");
    }

    void testAsyncReconnect() {
        QTemporaryDir tempDirectory;
        check(tempDirectory.isValid(), "temporary directory created");
        const QString socketPath = tempDirectory.filePath(QStringLiteral("events.sock"));
        QLocalServer::removeServer(socketPath);

        QLocalServer server;
        check(server.listen(socketPath), "test IPC server listens");

        QPointer<QLocalSocket> firstPeer;
        int                    connectionCount = 0;
        QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto* peer = server.nextPendingConnection();
                ++connectionCount;
                if (connectionCount == 1) {
                    firstPeer = peer;
                    peer->write(QByteArrayLiteral("{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":true}\n"
                                                  "{\"event\":\"subscribe_keyboard\",\"keyboard_id\":1,\"name\":\"test "
                                                  "keyboard\",\"depressed\":0,\"latched\":0,\"locked\":0,\"group\":0,\"keymap\":\"xkb\",\"rate\":25,\"delay\":600}\n"
                                                  "{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":1,\"keycode\":30,\"state\":\"pressed\"}\n"));
                    peer->flush();
                } else if (connectionCount == 2) {
                    peer->write(QByteArrayLiteral("{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":false}\n"
                                                  "{\"event\":\"subscribe_keyboard\",\"keyboard_id\":2,\"name\":\"reconnected "
                                                  "keyboard\",\"depressed\":0,\"latched\":0,\"locked\":0,\"group\":0,\"keymap\":\"xkb\",\"rate\":30,\"delay\":500}\n"));
                    peer->flush();
                }
            }
        });

        IpcClient        client(socketPath);
        std::vector<int> deliveredEvents;
        int              resetNotifications = 0;
        QObject::connect(
            &client, &IpcClient::protocolMessageReceived, &client,
            [&](const ProtocolMessage& message) {
                deliveredEvents.push_back(static_cast<int>(message.index()));
                if (std::holds_alternative<KeyMessage>(message)) {
                    check(client.keyboardCount() == 1, "message is delivered after its connection-state transition");
                }
            },
            Qt::DirectConnection);
        QObject::connect(&client, &IpcClient::connectionReset, &client, [&] { ++resetNotifications; }, Qt::DirectConnection);
        client.start();
        check(waitUntil([&] { return client.connected() && client.hasCastingState() && client.keyboardCount() == 1 && client.lastEventText().contains(QStringLiteral("pressed")); },
                        2000),
              "async client connects and consumes coalesced state messages");
        check(deliveredEvents == std::vector<int>{0, 1, 3}, "validated protocol messages are delivered in wire order");
        check(client.paused(), "casting pause state received over socket");
        check(!firstPeer.isNull(), "first server-side connection accepted");

        firstPeer->abort();
        check(waitUntil([&] { return !client.connected() && !client.hasCastingState() && client.keyboardCount() == 0; }, 1000), "disconnect clears connection state");
        check(resetNotifications > 0, "disconnect explicitly notifies consumers to reset held input state");
        check(waitUntil([&] { return connectionCount >= 2 && client.connected() && client.hasCastingState() && client.keyboardCount() == 1; }, 3000),
              "client reconnects after bounded delay and accepts a fresh snapshot");
        check(!client.paused(), "reconnected casting snapshot replaces stale pause state");
        check(client.detailsText().contains(QStringLiteral("reconnected keyboard")), "reconnected keyboard snapshot replaces prior registry");
        check(deliveredEvents == std::vector<int>{0, 1, 3, 0, 1}, "reconnected messages continue in wire order");

        server.close();
        QLocalServer::removeServer(socketPath);
    }

    void testRejectedTransitionIsNotDelivered() {
        QTemporaryDir tempDirectory;
        check(tempDirectory.isValid(), "invalid-transition temporary directory created");
        const QString socketPath = tempDirectory.filePath(QStringLiteral("invalid-transition.sock"));
        QLocalServer::removeServer(socketPath);

        QLocalServer server;
        check(server.listen(socketPath), "invalid-transition test server listens");
        QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto* peer = server.nextPendingConnection();
                peer->write(QByteArrayLiteral("{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":false}\n"
                                              "{\"event\":\"key\",\"keyboard_id\":7,\"time_ms\":1,\"keycode\":30,\"state\":\"pressed\"}\n"));
                peer->flush();
            }
        });

        IpcClient        client(socketPath);
        std::vector<int> deliveredEvents;
        bool             rejected = false;
        QObject::connect(
            &client, &IpcClient::protocolMessageReceived, &client, [&](const ProtocolMessage& message) { deliveredEvents.push_back(message.index()); }, Qt::DirectConnection);
        QObject::connect(&client, &IpcClient::protocolError, &client, [&](const QString&) { rejected = true; }, Qt::DirectConnection);
        client.start();
        check(waitUntil([&] { return rejected; }, 1000), "invalid connection-state transition is rejected");
        check(deliveredEvents == std::vector<int>{0}, "rejected transition never reaches the keyboard interpreter");

        server.close();
        QLocalServer::removeServer(socketPath);
    }
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testLineFraming();
    testMessageParsing();
    testConnectionState();
    testRetryUntilServerAppears();
    testAsyncReconnect();
    testRejectedTransitionIsNotDelivered();
    std::cout << "overlay IPC tests passed\n";
    return 0;
}
