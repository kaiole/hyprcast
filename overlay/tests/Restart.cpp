#include "ipc/Restart.hpp"
#include "ipc/OverlayInstance.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <xkbcommon/xkbcommon.h>

using namespace Hyprcast::Overlay;
namespace {
    QString executable;
    void check(bool value, const char *message) {
        if (!value)
            throw std::runtime_error(message);
    }
    bool until(const std::function<bool()> &predicate, int budget = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < budget) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        return predicate();
    }
    void write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "open fixture file");
        check(file.write(bytes) == bytes.size(), "write fixture file");
    }
    void state(QLocalSocket &peer, bool paused) {
        peer.write(paused ? "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":true}\n"
                          : "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":false}\n");
        peer.flush();
    }
    struct Fixture {
        QTemporaryDir directory;
        QString socket = directory.filePath("events.sock");
        QString config = directory.filePath("overlay.toml");
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        QProcess resident;
        QLocalServer plugin;
        std::unique_ptr<QLocalSocket> peer;
        QByteArray log;
        Fixture() {
            check(directory.isValid(), "temporary directory");
            environment.insert("QT_QPA_PLATFORM", "offscreen");
            environment.insert("QT_FORCE_STDERR_LOGGING", "1");
            environment.insert("QT_QUICK_BACKEND", "software");
            environment.insert("QT_LOGGING_RULES", "*.debug=false;*.info=true;*.warning=true;*.critical=true");
            environment.insert("XDG_CONFIG_HOME", directory.path());
            environment.insert("XDG_DATA_HOME", directory.path());
            environment.insert("XDG_DATA_DIRS", directory.path());
            environment.remove("HYPRCAST_PRIVATE_REEXEC_FD");
            write(config, "[window]\nwidth = 610\n");
        }
        ~Fixture() {
            resident.kill();
            resident.waitForFinished();
        }
        void start(bool active = false, bool available = true, bool acknowledge = true) {
            if (available)
                check(plugin.listen(socket), "mock plugin listen");
            resident.setProcessEnvironment(environment);
            resident.setWorkingDirectory(directory.path());
            QStringList args{"--socket", socket, "--config", config, "--width", "777"};
            if (active)
                args << "toggle";
            resident.start(executable, args);
            check(resident.waitForStarted(), "start overlay");
            if (available) {
                accept();
                state(*peer, true);
                if (active) {
                    check(until([&] { return peer->canReadLine(); }), "first activation");
                    check(peer->readLine() == "enable\n", "initial enable");
                    if (acknowledge)
                        state(*peer, false);
                }
            }
            check(until([&] {
                      log += resident.readAllStandardError();
                      return log.contains("777x");
                  }),
                  "startup with original explicit width override");
        }
        void accept() {
            check(until([&] { return plugin.hasPendingConnections(); }), "plugin connection");
            peer.reset(plugin.nextPendingConnection());
        }
        void command(QProcess &process, const QStringList &extra = {}, QString name = "restart") {
            process.setProcessEnvironment(environment);
            process.setWorkingDirectory("/");
            process.start(executable, QStringList{name, "--socket", socket} + extra);
            check(process.waitForStarted(), "start caller");
        }
        int result(QProcess &process, int budget = RestartReplyMs + 4000) {
            check(until([&] { return process.state() == QProcess::NotRunning; }, budget), "bounded command reply");
            if (process.exitCode() != 0)
                std::cerr << process.readAllStandardError().constData();
            return process.exitCode();
        }
        void replacement() {
            check(until([&] { return peer->state() == QLocalSocket::UnconnectedState; }), "exec closes old event connection (plugin pauses here)");
            accept();
        }
    };
    void testRestart() {
        Fixture f;
        f.start(true);
        const auto pid = f.resident.processId();
        QProcess caller;
        f.command(caller);
        f.replacement();
        state(*f.peer, true);
        check(until([&] { return f.peer->canReadLine(); }), "restore enable requested");
        check(f.peer->readLine() == "enable\n", "restore is explicit enable");
        check(caller.state() != QProcess::NotRunning, "no premature restart success");
        QProcess busy, launch, toggle;
        f.command(busy);
        f.command(toggle, {}, "toggle");
        launch.setProcessEnvironment(f.environment);
        launch.start(executable, {"--socket", f.socket});
        check(f.result(busy) == 1, "concurrent restart rejected");
        check(f.result(toggle) == 1, "concurrent capture rejected");
        check(f.result(launch) == 1, "ownership retained through exec");
        state(*f.peer, false);
        check(f.result(caller) == 0, "active restoration confirmed");
        check(f.resident.processId() == pid && f.resident.state() == QProcess::Running, "same resident PID, new runtime");

        QProcess departed;
        f.command(departed);
        f.replacement();
        state(*f.peer, true);
        check(until([&] { return f.peer->canReadLine(); }), "requester-exit restoration");
        check(f.peer->readLine() == "enable\n", "requester-exit explicit enable");
        departed.kill();
        departed.waitForFinished();
        state(*f.peer, false);
        QElapsedTimer settled;
        settled.start();
        until([&] { return settled.elapsed() > 100; });
        check(f.resident.state() == QProcess::Running, "departed requester does not crash replacement");

        // Switch to paused, then repeatedly exec without ever enabling.
        QProcess off;
        f.command(off, {}, "toggle");
        check(until([&] { return f.peer->canReadLine(); }), "pause command");
        check(f.peer->readLine() == "disable\n", "pause target");
        state(*f.peer, true);
        check(f.result(off) == 0, "pause confirmed");
        const int fdCount = QDir(QStringLiteral("/proc/%1/fd").arg(pid)).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
        for (int i = 0; i < 3; ++i) {
            QProcess again;
            f.command(again);
            f.replacement();
            // An initially active snapshot must be explicitly disabled, never shown.
            state(*f.peer, i == 0 ? false : true);
            if (i == 0) {
                check(until([&] { return f.peer->canReadLine(); }), "paused target reconciled");
                check(f.peer->readLine() == "disable\n", "paused restart never enables");
                state(*f.peer, true);
            }
            check(f.result(again) == 0, "paused restart complete");
            check(!f.peer->bytesAvailable(), "no stray enable intent");
        }
        const int finalFdCount = QDir(QStringLiteral("/proc/%1/fd").arg(pid)).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
        check(finalFdCount <= fdCount + 1, "repeated restarts do not leak descriptors");

        // Fail restoration, disconnect to pause, then do not replay on reconnect.
        QProcess on;
        f.command(on, {}, "toggle");
        check(until([&] { return f.peer->canReadLine(); }), "enable before timeout test");
        f.peer->readLine();
        state(*f.peer, false);
        check(f.result(on) == 0, "enable confirmed");
        QProcess timeout;
        f.command(timeout);
        f.replacement();
        state(*f.peer, true);
        check(until([&] { return f.peer->canReadLine(); }), "unacknowledged restore request");
        check(f.peer->readLine() == "enable\n", "one restore request");
        check(f.result(timeout) == 1, "capture restoration timeout is failure");
        check(until([&] { return f.peer->state() == QLocalSocket::UnconnectedState; }), "uncertain enable disconnects to pause");
        f.accept();
        state(*f.peer, true);
        QElapsedTimer quiet;
        quiet.start();
        until([&] { return quiet.elapsed() > 300; });
        check(!f.peer->bytesAvailable(), "no delayed enable after restoration failure");
    }
    void testSessionSelectors() {
        Fixture selected, other;
        selected.socket = selected.directory.filePath("hyprcast/session/events.sock");
        check(QDir().mkpath(QFileInfo(selected.socket).absolutePath()), "session socket directory");
        selected.environment.insert("XDG_RUNTIME_DIR", selected.directory.path());
        selected.environment.insert("HYPRLAND_INSTANCE_SIGNATURE", "wrong-session");
        selected.start();
        other.start();
        QProcess caller;
        caller.setProcessEnvironment(selected.environment);
        caller.start(executable, {"restart", "--instance-signature", "session"});
        selected.replacement();
        state(*selected.peer, true);
        check(selected.result(caller) == 0, "signature selector routes to selected session");
        check(other.peer->state() == QLocalSocket::ConnectedState, "restart never broadcasts to another session");
        QProcess precedence;
        selected.command(precedence, {"--instance-signature", "invalid/session"});
        selected.replacement();
        state(*selected.peer, true);
        check(selected.result(precedence) == 0, "explicit socket retains existing selector precedence");
    }
    void testInitialBusy() {
        Fixture f;
        f.start(true, true, false);
        QProcess busy;
        f.command(busy);
        check(f.result(busy) == 1 && f.peer->state() == QLocalSocket::ConnectedState, "first activation pending rejects restart");
        state(*f.peer, false);
        QProcess pause;
        f.command(pause, {}, "toggle");
        check(until([&] { return f.peer->canReadLine(); }), "rejected restart does not lose initial capture intent");
        check(f.peer->readLine() == "disable\n", "acknowledged initial enable remains authoritative");
        state(*f.peer, true);
        check(f.result(pause) == 0, "capture remains usable after busy restart");
    }
    void testConfiguration() {
        Fixture f;
        f.start();
        const auto pid = f.resident.processId();
        write(f.config, "[window]\nwidth = 'invalid'\n");
        QProcess invalid;
        f.command(invalid);
        check(f.result(invalid) == 1, "invalid config fails preflight");
        check(f.peer->state() == QLocalSocket::ConnectedState && f.resident.processId() == pid, "preflight preserves running overlay");
        QFile::remove(f.config);
        QProcess missing;
        f.command(missing);
        check(f.result(missing) == 1, "missing required config fails preflight");
        check(f.peer->state() == QLocalSocket::ConnectedState, "missing required config does not exec");
        const QString theme = f.directory.filePath("hyprcast/themes/fresh");
        check(QDir().mkpath(theme), "theme package directory");
        write(theme + "/theme.toml",
              "[theme]\nid = 'fresh'\nname = 'Fresh'\napi_version = 1\nentry = 'Main.qml'\n[options.word]\ntype = 'string'\ndefault = 'one'\n");
        write(theme + "/asset.ppm", "P3\n1 1\n255\n255 0 0\n");
        write(theme + "/Helper.qml", "import QtQuick\nItem { property string word: 'helper-one' }\n");
        write(theme + "/Main.qml",
              "import QtQuick\nItem { Helper { id: helper } Image { source: 'asset.ppm'; asynchronous: false; onStatusChanged: if (status === Image.Ready) "
              "console.warn('ASSET', sourceSize.width) } Component.onCompleted: console.warn('FRESH-one', helper.word, hyprcast.options.word) }\n");
        write(f.config, "[window]\nwidth = 620\nclick_through = false\n[theme]\nid = 'fresh'\n");
        QProcess fresh;
        f.command(fresh);
        f.replacement();
        state(*f.peer, true);
        check(f.result(fresh) == 0, "new package discovered, restart-only setting applied");
        check(until([&] {
                  f.log += f.resident.readAllStandardError();
                  return f.log.contains("FRESH-one helper-one one") && f.log.contains("theme=fresh");
              }),
              "fresh package instantiated");
        check(f.log.contains("777x"), "original CLI overrides survive different caller cwd");
        // Same ID/path changes must load new QML, helper and manifest defaults.
        write(theme + "/theme.toml",
              "[theme]\nid = 'fresh'\nname = 'Fresh'\napi_version = 1\nentry = 'Main.qml'\n[options.word]\ntype = 'string'\ndefault = 'two'\n");
        write(theme + "/Helper.qml", "import QtQuick\nItem { property string word: 'helper-two' }\n");
        write(theme + "/asset.ppm", "P3\n2 1\n255\n0 255 0 0 0 255\n");
        write(theme + "/Main.qml",
              "import QtQuick\nItem { Helper { id: helper } Image { source: 'asset.ppm'; asynchronous: false; onStatusChanged: if (status === Image.Ready) "
              "console.warn('ASSET', sourceSize.width) } Component.onCompleted: console.warn('FRESH-two', helper.word, hyprcast.options.word) }\n");
        QProcess changed;
        f.command(changed);
        f.replacement();
        state(*f.peer, true);
        check(f.result(changed) == 0, "same-ID package update restart");
        check(until([&] {
                  f.log += f.resident.readAllStandardError();
                  return f.log.count("FRESH-two helper-two two") >= 2;
              }),
              "isolated preflight AND new runtime use fresh content");
        check(f.log.contains("ASSET 2"), "replacement observes same-path image asset update (Qt's image cache can be process-global)");
        write(theme + "/Main.qml", "not valid QML\n");
        QProcess badQml;
        f.command(badQml);
        check(f.result(badQml) == 1 && f.peer->state() == QLocalSocket::ConnectedState, "bad QML rejected without replacement");
        QProcess misuse;
        f.command(misuse, {"--width", "999"});
        check(f.result(misuse) == 2 && f.peer->state() == QLocalSocket::ConnectedState, "restart override misuse never mutates resident");
        QProcess unknown;
        f.command(unknown, {"--not-an-option"});
        check(f.result(unknown) == 2, "unknown restart option is CLI misuse");
        write(f.config, "[window]\nmonitor='not-an-output'\n");
        QProcess monitor;
        f.command(monitor);
        check(f.result(monitor) == 1 && f.peer->state() == QLocalSocket::ConnectedState, "unavailable output rejected without exec");
    }
    void testHistoryAndSpaceReload() {
        Fixture f;
        const QString theme = f.directory.filePath("hyprcast/themes/input-test");
        check(QDir().mkpath(theme), "input test theme directory");
        write(theme + "/theme.toml", "[theme]\nid='input-test'\nname='Input test'\napi_version=1\nentry='Main.qml'\n");
        write(theme + "/Main.qml",
              "import QtQuick\nItem { Component.onCompleted: console.warn('INITIAL', hyprcast.historyCount, hyprcast.heldKeyCount); Connections { target: "
              "hyprcast.displayHistory; function onDisplayTextChanged() { console.warn('HISTORY', hyprcast.displayHistory.displayText) } } Connections { "
              "target: hyprcast; function onHeldKeysChanged() { console.warn('HELD', hyprcast.heldKeyCount) } } }\n");
        const QByteArray base = "[theme]\nid='input-test'\n[expiration]\nafter_ms=0\n";
        write(f.config, base);
        f.start(true);
        xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        check(context, "XKB context");
        xkb_rule_names names{};
        names.rules = "evdev";
        names.model = "pc105";
        names.layout = "us";
        xkb_keymap *keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        check(keymap, "XKB keymap");
        char *text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
        check(text, "serialize keymap");
        QJsonObject snapshot{{"event", "subscribe_keyboard"},
                             {"keyboard_id", 1},
                             {"name", "test"},
                             {"depressed", 0},
                             {"latched", 0},
                             {"locked", 0},
                             {"group", 0},
                             {"rate", 0},
                             {"delay", 0},
                             {"keymap", QString::fromUtf8(text)}};
        std::free(text);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        const QByteArray frame = QJsonDocument(snapshot).toJson(QJsonDocument::Compact) + '\n';
        f.peer->write(frame);
        f.peer->write("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":1,\"keycode\":57,\"state\":\"pressed\"}\n");
        f.peer->flush();
        check(until([&] {
                  f.log += f.resident.readAllStandardError();
                  return f.log.contains("HELD 1");
              }),
              "held key observed");
        write(f.config, base + "[symbols]\nspace='<space>'\n");
        check(until([&] {
                  f.log += f.resident.readAllStandardError();
                  return f.log.contains("HISTORY <space>");
              }),
              "space-only TOML edit applies live to existing history");
        QProcess caller;
        f.command(caller);
        f.replacement();
        state(*f.peer, true);
        check(until([&] { return f.peer->canReadLine(); }), "history restart restoration");
        check(f.peer->readLine() == "enable\n", "history restart enable");
        state(*f.peer, false);
        check(f.result(caller) == 0, "history restart success");
        f.log.clear();
        f.log += f.resident.readAllStandardError();
        check(f.log.contains("INITIAL 0 0"), "fresh history and held-key state");
        f.peer->write(frame);
        f.peer->flush();
        QElapsedTimer quiet;
        quiet.start();
        until([&] {
            f.log += f.resident.readAllStandardError();
            return quiet.elapsed() > 200;
        });
        check(!f.log.contains("HELD 1") && !f.log.contains("HISTORY <space>"), "snapshot does not reconstruct old held keys/history");
    }
    void testUnavailable() {
        Fixture f;
        QProcess absent;
        f.command(absent);
        check(f.result(absent) == 1 && !QFile::exists(f.socket + ".overlay"), "no resident restart does not start one");
        f.start(false, false);
        QProcess local;
        f.command(local);
        check(f.result(local) == 0, "local restart succeeds without plugin");
        check(f.plugin.listen(f.socket), "late plugin listen");
        f.accept();
        state(*f.peer, true);
        QElapsedTimer quiet;
        quiet.start();
        until([&] { return quiet.elapsed() > 400; });
        check(!f.peer->bytesAvailable(), "unavailable plugin restart has no pending enable");
    }
    void testFailedExecAndMigration() {
        QTemporaryDir dir;
        const QString path = dir.filePath("event.sock");
        OverlayInstance owner(path);
        QString error;
        check(owner.acquire(false, &error) == OverlayInstance::Result::Owner, "fixture owner");
        QLocalSocket requester;
        requester.connectToServer(path + ".overlay");
        check(requester.waitForConnected(), "handoff requester connects");
        QObject::connect(&owner, &OverlayInstance::restartRequested, &owner, [&](QLocalSocket *client) {
            RestartContext context;
            context.socketPath = path;
            context.configPath = dir.filePath("config");
            context.deadline = monotonicMilliseconds() + RestartBudgetMs;
            RestartHandoff handoff;
            check(!handoff.execute(context, owner.lockDescriptor(), int(client->socketDescriptor()), dir.filePath("not-an-executable"), &error),
                  "exec failure returns");
            OverlayInstance::reply(client, error);
        });
        requester.write("restart\n");
        requester.flush();
        check(until([&] { return requester.canReadLine(); }), "exec failure response");
        check(requester.readLine().contains("Executable replacement failed"), "exec failure distinguished");
        OverlayInstance duplicate(path);
        check(duplicate.acquire(false, &error) == OverlayInstance::Result::Error, "failed exec retains ownership");

        // Old servers need not hold the new flock. A live socket is never stale.
        const QString oldPath = dir.filePath("old.sock");
        QLocalServer old;
        check(old.listen(oldPath + ".overlay"), "old resident fixture");
        OverlayInstance newLaunch(oldPath);
        check(newLaunch.acquire(false, &error) == OverlayInstance::Result::Error, "legacy live socket not taken over");
        check(QFile::exists(oldPath + ".overlay"), "legacy socket remains");
        QObject::connect(&old, &QLocalServer::newConnection, &old, [&] {
            while (auto *client = old.nextPendingConnection()) {
                QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                QObject::connect(client, &QLocalSocket::readyRead, client, [client] {
                    client->readAll();
                    OverlayInstance::reply(client, QStringLiteral("Invalid overlay command"));
                });
            }
        });
        Fixture environment;
        QProcess unsupported;
        unsupported.setProcessEnvironment(environment.environment);
        unsupported.start(executable, {"restart", "--socket", oldPath});
        check(until([&] { return unsupported.state() == QProcess::NotRunning; }), "unsupported old resident response");
        check(unsupported.exitCode() == 1 && unsupported.readAllStandardError().contains("does not support restart"),
              "upgrade diagnostic without kill/takeover");

        for (const QByteArray response : {QByteArray{}, QByteArray("ok\nextra\n")}) {
            const QString badPath = dir.filePath(response.isEmpty() ? "disconnected.sock" : "malformed.sock");
            QLocalServer bad;
            check(bad.listen(badPath + ".overlay"), "bad response fixture");
            QObject::connect(&bad, &QLocalServer::newConnection, &bad, [&] {
                while (auto *client = bad.nextPendingConnection()) {
                    QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                    QObject::connect(client, &QLocalSocket::readyRead, client, [client, response] {
                        client->readAll();
                        client->write(response);
                        client->disconnectFromServer();
                    });
                }
            });
            QProcess caller;
            caller.setProcessEnvironment(environment.environment);
            caller.start(executable, {"restart", "--socket", badPath});
            check(until([&] { return caller.state() == QProcess::NotRunning; }), "bad response caller exits boundedly");
            check(caller.exitCode() == 1 && caller.readAllStandardError().contains("outcome unknown"), "disconnect/malformed reply never means success");
        }
    }
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        check(app.arguments().size() == 2, "overlay executable argument");
        executable = app.arguments().at(1);
        testSessionSelectors();
        testInitialBusy();
        testRestart();
        testConfiguration();
        testHistoryAndSpaceReload();
        testUnavailable();
        testFailedExecAndMigration();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "check failed: " << error.what() << '\n';
        return 1;
    }
}
