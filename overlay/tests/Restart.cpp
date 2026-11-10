#include "ipc/Restart.hpp"

#include "Support.hpp"
#include "hyprcast/os/unique_fd.hpp"
#include "hyprcast/protocol/wire.hpp"
#include "ipc/OverlayInstance.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/file.h>
#include <xkbcommon/xkbcommon.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace hyprcast::overlay;
using tests::waitFor;
namespace {

// The overlay binary under test.
const QString& executable() {
    static const QString path = QStringLiteral(HYPRCAST_OVERLAY_BINARY);
    return path;
}
void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        << "open fixture file";
    ASSERT_EQ(file.write(bytes), bytes.size()) << "write fixture file";
}
void state(QLocalSocket& peer, bool paused) {
    peer.write(paused ? "{\"event\":\"casting_state\",\"keyboard_id\":0,"
                        "\"paused\":true}\n"
                      : "{\"event\":\"casting_state\",\"keyboard_id\":0,"
                        "\"paused\":false}\n");
    peer.flush();
}
// Waits for a caller to exit and returns its exit code, or -1 if it does not
// answer in time.
int result(QProcess& process) {
    const int budget =
        static_cast<int>(RestartReplyTimeout.count()) + tests::WaitTimeoutMs;
    if (!waitFor([&] { return process.state() == QProcess::NotRunning; },
                 budget)) {
        ADD_FAILURE() << "bounded command reply";
        return -1;
    }
    if (process.exitCode() != 0) {
        std::cerr << process.readAllStandardError().constData();
    }
    return process.exitCode();
}
struct Fixture {
    QTemporaryDir directory;
    QString socket = directory.filePath("events.sock");
    QString config = directory.filePath("overlay.toml");
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QProcess resident;
    QLocalServer plugin;
    std::unique_ptr<QLocalSocket> peer;
    // What the mock plugin opens every connection with.
    QByteArray greeting = R"({"event":"hello","protocol_version":)"
        + QByteArray::number(hyprcast::wire::protocol_version) + "}\n";
    QByteArray log;
    Fixture() { prepare(); }
    ~Fixture() {
        resident.kill();
        resident.waitForFinished();
    }
    Q_DISABLE_COPY_MOVE(Fixture)
    // Not in the constructor: ASSERT_* needs a void function.
    void prepare() {
        ASSERT_TRUE(directory.isValid()) << "temporary directory";
        // Starts from the environment of this process, which main has already
        // moved away from the live session.
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("QT_FORCE_STDERR_LOGGING", "1");
        environment.insert("QT_QUICK_BACKEND", "software");
        environment.insert(
            "QT_LOGGING_RULES",
            "*.debug=false;*.info=true;*.warning=true;*.critical=true");
        environment.insert("XDG_CONFIG_HOME", directory.path());
        environment.insert("XDG_DATA_HOME", directory.path());
        environment.insert("XDG_DATA_DIRS", directory.path());
        environment.remove("HYPRCAST_PRIVATE_REEXEC_FD");
        write(config, "[window]\nwidth = 610\n");
    }
    void
    start(bool active = false, bool available = true, bool acknowledge = true) {
        if (available) {
            ASSERT_TRUE(plugin.listen(socket)) << "mock plugin listen";
        }
        launch(active);
        if (available) {
            accept();
            state(*peer, true);
            if (active) {
                ASSERT_TRUE(waitFor([&] { return peer->canReadLine(); }))
                    << "first activation";
                ASSERT_EQ(peer->readLine(), "enable\n") << "initial enable";
                if (acknowledge) {
                    state(*peer, false);
                }
            }
        }
        ASSERT_TRUE(waitFor([&] {
            log += resident.readAllStandardError();
            return log.contains("777x");
        })) << "startup with original explicit width override";
    }
    void launch(bool active) {
        resident.setProcessEnvironment(environment);
        resident.setWorkingDirectory(directory.path());
        QStringList
            args {"--socket", socket, "--config", config, "--width", "777"};
        if (active) {
            args << "toggle";
        }
        resident.start(executable(), args);
        ASSERT_TRUE(resident.waitForStarted()) << "start overlay";
    }
    void accept() {
        ASSERT_TRUE(waitFor([&] { return plugin.hasPendingConnections(); }))
            << "plugin connection";
        peer.reset(plugin.nextPendingConnection());
        peer->write(greeting);
    }
    void command(QProcess& process,
                 const QStringList& extra = {},
                 const QString& name = "restart") {
        process.setProcessEnvironment(environment);
        process.setWorkingDirectory("/");
        process.start(executable(),
                      QStringList {name, "--socket", socket} + extra);
        ASSERT_TRUE(process.waitForStarted()) << "start caller";
    }
    void replacement() {
        ASSERT_TRUE(waitFor([&] {
            return peer->state() == QLocalSocket::UnconnectedState;
        })) << "exec closes old event connection (plugin pauses here)";
        accept();
    }
};
TEST(OverlayRestart, ActiveRestartRestoresCapture) {
    Fixture f;
    f.start(true);
    const auto pid = f.resident.processId();
    QProcess caller;
    f.command(caller);
    f.replacement();
    state(*f.peer, true);
    ASSERT_TRUE(waitFor([&] { return f.peer->canReadLine(); }))
        << "restore enable requested";
    ASSERT_EQ(f.peer->readLine(), "enable\n") << "restore is explicit enable";
    ASSERT_NE(caller.state(), QProcess::NotRunning)
        << "no premature restart success";
    QProcess busy;
    QProcess launch;
    QProcess toggle;
    f.command(busy);
    f.command(toggle, {}, "toggle");
    launch.setProcessEnvironment(f.environment);
    launch.start(executable(), {"--socket", f.socket});
    ASSERT_EQ(result(busy), 1) << "concurrent restart rejected";
    ASSERT_EQ(result(toggle), 1) << "concurrent capture rejected";
    ASSERT_EQ(result(launch), 1) << "ownership retained through exec";
    state(*f.peer, false);
    ASSERT_EQ(result(caller), 0) << "active restoration confirmed";
    ASSERT_TRUE(f.resident.processId() == pid
                && f.resident.state() == QProcess::Running)
        << "same resident PID, new runtime";
}

TEST(OverlayRestart, DepartedRequesterLeavesTheReplacementRunning) {
    Fixture f;
    f.start(true);
    QProcess departed;
    f.command(departed);
    f.replacement();
    state(*f.peer, true);
    ASSERT_TRUE(waitFor([&] { return f.peer->canReadLine(); }))
        << "requester-exit restoration";
    ASSERT_EQ(f.peer->readLine(), "enable\n")
        << "requester-exit explicit enable";
    departed.kill();
    departed.waitForFinished();
    state(*f.peer, false);
    tests::runEventLoopFor(100);
    ASSERT_EQ(f.resident.state(), QProcess::Running)
        << "departed requester does not crash replacement";
}

TEST(OverlayRestart, PausedRestartNeverEnables) {
    Fixture f;
    f.start(true);
    const auto pid = f.resident.processId();
    // Switch to paused, then repeatedly exec without ever enabling.
    QProcess off;
    f.command(off, {}, "toggle");
    ASSERT_TRUE(waitFor([&] { return f.peer->canReadLine(); }))
        << "pause command";
    ASSERT_EQ(f.peer->readLine(), "disable\n") << "pause target";
    state(*f.peer, true);
    ASSERT_EQ(result(off), 0) << "pause confirmed";
    const auto openDescriptors = [pid] {
        return QDir(QStringLiteral("/proc/%1/fd").arg(pid))
            .entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
            .size();
    };
    qsizetype fdCount = 0;
    for (int i = 0; i < 3; ++i) {
        QProcess again;
        f.command(again);
        f.replacement();
        // An initially active snapshot must be explicitly disabled, never
        // shown.
        state(*f.peer, i != 0);
        if (i == 0) {
            ASSERT_TRUE(waitFor([&] { return f.peer->canReadLine(); }))
                << "paused target reconciled";
            ASSERT_EQ(f.peer->readLine(), "disable\n")
                << "paused restart never enables";
            state(*f.peer, true);
        }
        ASSERT_EQ(result(again), 0) << "paused restart complete";
        ASSERT_EQ(f.peer->bytesAvailable(), 0) << "no stray enable intent";
        if (i == 0) {
            // Counted once the resident is a replacement, as every later one
            // is.
            fdCount = openDescriptors();
        }
    }
    ASSERT_LE(openDescriptors(), fdCount + 1)
        << "repeated restarts do not leak descriptors";
}

TEST(OverlayRestart, FailedRestorationIsNotReplayed) {
    Fixture f;
    f.start();
    // Fail restoration, disconnect to pause, then do not replay on reconnect.
    QProcess on;
    f.command(on, {}, "toggle");
    ASSERT_TRUE(waitFor([&] { return f.peer->canReadLine(); }))
        << "enable before timeout test";
    f.peer->readLine();
    state(*f.peer, false);
    ASSERT_EQ(result(on), 0) << "enable confirmed";
    QProcess timeout;
    f.command(timeout);
    f.replacement();
    state(*f.peer, true);
    ASSERT_TRUE(waitFor([&] { return f.peer->canReadLine(); }))
        << "unacknowledged restore request";
    ASSERT_EQ(f.peer->readLine(), "enable\n") << "one restore request";
    ASSERT_EQ(result(timeout), 1) << "capture restoration timeout is failure";
    ASSERT_TRUE(waitFor([&] {
        return f.peer->state() == QLocalSocket::UnconnectedState;
    })) << "uncertain enable disconnects to pause";
    f.accept();
    state(*f.peer, true);
    tests::runEventLoopFor(300);
    ASSERT_EQ(f.peer->bytesAvailable(), 0)
        << "no delayed enable after restoration failure";
}

TEST(OverlayRestart, SignatureSelectsTheSession) {
    Fixture selected;
    Fixture other;
    selected.socket =
        selected.directory.filePath("hyprcast/session/events.sock");
    ASSERT_TRUE(QDir().mkpath(QFileInfo(selected.socket).absolutePath()))
        << "session socket directory";
    selected.environment.insert("XDG_RUNTIME_DIR", selected.directory.path());
    selected.environment.insert("HYPRLAND_INSTANCE_SIGNATURE", "wrong-session");
    selected.start();
    other.start();
    QProcess caller;
    caller.setProcessEnvironment(selected.environment);
    caller.start(executable(), {"restart", "--instance-signature", "session"});
    selected.replacement();
    state(*selected.peer, true);
    ASSERT_EQ(result(caller), 0)
        << "signature selector routes to selected session";
    ASSERT_EQ(other.peer->state(), QLocalSocket::ConnectedState)
        << "restart never broadcasts to another session";
    QProcess precedence;
    selected.command(precedence, {"--instance-signature", "invalid/session"});
    selected.replacement();
    state(*selected.peer, true);
    ASSERT_EQ(result(precedence), 0)
        << "explicit socket retains existing selector precedence";
}

TEST(OverlayRestart, PendingFirstActivationRejectsRestart) {
    Fixture f;
    f.start(true, true, false);
    QProcess busy;
    f.command(busy);
    ASSERT_TRUE(result(busy) == 1
                && f.peer->state() == QLocalSocket::ConnectedState)
        << "first activation pending rejects restart";
    state(*f.peer, false);
    QProcess pause;
    f.command(pause, {}, "toggle");
    ASSERT_TRUE(waitFor([&] { return f.peer->canReadLine(); }))
        << "rejected restart does not lose initial capture intent";
    ASSERT_EQ(f.peer->readLine(), "disable\n")
        << "acknowledged initial enable remains authoritative";
    state(*f.peer, true);
    ASSERT_EQ(result(pause), 0) << "capture remains usable after busy restart";
}

TEST(OverlayRestart, BadConfigFailsPreflight) {
    Fixture f;
    f.start();
    const auto pid = f.resident.processId();
    write(f.config, "[window]\nwidth = 'invalid'\n");
    QProcess invalid;
    f.command(invalid);
    ASSERT_EQ(result(invalid), 1) << "invalid config fails preflight";
    ASSERT_TRUE(f.peer->state() == QLocalSocket::ConnectedState
                && f.resident.processId() == pid)
        << "preflight preserves running overlay";
    QFile::remove(f.config);
    QProcess missing;
    f.command(missing);
    ASSERT_EQ(result(missing), 1) << "missing required config fails preflight";
    ASSERT_EQ(f.peer->state(), QLocalSocket::ConnectedState)
        << "missing required config does not exec";
}

TEST(OverlayRestart, LoadsFreshThemeContent) {
    Fixture f;
    f.start();
    const QString theme = f.directory.filePath("hyprcast/themes/fresh");
    ASSERT_TRUE(QDir().mkpath(theme)) << "theme package directory";
    write(theme + "/theme.toml",
          "[theme]\nid = 'fresh'\nname = 'Fresh'\napi_version = 1\nentry = "
          "'Main.qml'\n[options.word]\ntype = 'string'\ndefault = 'one'\n");
    write(theme + "/asset.ppm", "P3\n1 1\n255\n255 0 0\n");
    write(theme + "/Helper.qml",
          "import QtQuick\nItem { property string word: 'helper-one' }\n");
    write(theme + "/Main.qml",
          "import QtQuick\nItem { Helper { id: helper } Image { source: "
          "'asset.ppm'; asynchronous: false; onStatusChanged: if (status === "
          "Image.Ready) "
          "console.warn('ASSET', sourceSize.width) } Component.onCompleted: "
          "console.warn('FRESH-one', helper.word, hyprcast.options.word) }\n");
    write(f.config,
          "[window]\nwidth = 620\nclick_through = false\n[theme]\nid = "
          "'fresh'\n");
    QProcess fresh;
    f.command(fresh);
    f.replacement();
    state(*f.peer, true);
    ASSERT_EQ(result(fresh), 0)
        << "new package discovered, restart-only setting applied";
    ASSERT_TRUE(waitFor([&] {
        f.log += f.resident.readAllStandardError();
        return f.log.contains("FRESH-one helper-one one")
            && f.log.contains("theme=fresh");
    })) << "fresh package instantiated";
    ASSERT_TRUE(f.log.contains("777x"))
        << "original CLI overrides survive different caller cwd";
    // Same ID/path changes must load new QML, helper and manifest defaults.
    write(theme + "/theme.toml",
          "[theme]\nid = 'fresh'\nname = 'Fresh'\napi_version = 1\nentry = "
          "'Main.qml'\n[options.word]\ntype = 'string'\ndefault = 'two'\n");
    write(theme + "/Helper.qml",
          "import QtQuick\nItem { property string word: 'helper-two' }\n");
    write(theme + "/asset.ppm", "P3\n2 1\n255\n0 255 0 0 0 255\n");
    write(theme + "/Main.qml",
          "import QtQuick\nItem { Helper { id: helper } Image { source: "
          "'asset.ppm'; asynchronous: false; onStatusChanged: if (status === "
          "Image.Ready) "
          "console.warn('ASSET', sourceSize.width) } Component.onCompleted: "
          "console.warn('FRESH-two', helper.word, hyprcast.options.word) }\n");
    QProcess changed;
    f.command(changed);
    f.replacement();
    state(*f.peer, true);
    ASSERT_EQ(result(changed), 0) << "same-ID package update restart";
    ASSERT_TRUE(waitFor([&] {
        f.log += f.resident.readAllStandardError();
        return f.log.count("FRESH-two helper-two two") >= 2;
    })) << "isolated preflight AND new runtime use fresh content";
    ASSERT_TRUE(f.log.contains("ASSET 2"))
        << "replacement observes same-path image asset update (Qt's image "
           "cache "
           "can be process-global)";
    write(theme + "/Main.qml", "not valid QML\n");
    QProcess badQml;
    f.command(badQml);
    ASSERT_TRUE(result(badQml) == 1
                && f.peer->state() == QLocalSocket::ConnectedState)
        << "bad QML rejected without replacement";
}

TEST(OverlayRestart, MisuseAndUnavailableOutputAreRejected) {
    Fixture f;
    f.start();
    QProcess misuse;
    f.command(misuse, {"--width", "999"});
    ASSERT_TRUE(result(misuse) == 2
                && f.peer->state() == QLocalSocket::ConnectedState)
        << "restart override misuse never mutates resident";
    QProcess unknown;
    f.command(unknown, {"--not-an-option"});
    ASSERT_EQ(result(unknown), 2) << "unknown restart option is CLI misuse";
    write(f.config, "[window]\nmonitor='not-an-output'\n");
    QProcess monitor;
    f.command(monitor);
    ASSERT_TRUE(result(monitor) == 1
                && f.peer->state() == QLocalSocket::ConnectedState)
        << "unavailable output rejected without exec";
}

TEST(OverlayRestart, StartsWithFreshHistoryAndHeldKeys) {
    Fixture f;
    const QString theme = f.directory.filePath("hyprcast/themes/input-test");
    ASSERT_TRUE(QDir().mkpath(theme)) << "input test theme directory";
    write(theme + "/theme.toml",
          "[theme]\nid='input-test'\nname='Input "
          "test'\napi_version=1\nentry='Main.qml'\n");
    write(
        theme + "/Main.qml",
        "import QtQuick\nItem { Component.onCompleted: console.warn('INITIAL', "
        "hyprcast.historyCount, hyprcast.heldKeyCount); Connections { target: "
        "hyprcast.displayHistory; function onDisplayTextChanged() { "
        "console.warn('HISTORY', hyprcast.displayHistory.displayText) } } "
        "Connections { "
        "target: hyprcast; function onHeldKeysChanged() { console.warn('HELD', "
        "hyprcast.heldKeyCount) } } }\n");
    const QByteArray base =
        "[theme]\nid='input-test'\n[expiration]\nafter_ms=0\n";
    write(f.config, base);
    f.start(true);
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    ASSERT_NE(context, nullptr) << "XKB context";
    xkb_rule_names names {};
    names.rules = "evdev";
    names.model = "pc105";
    names.layout = "us";
    xkb_keymap* keymap =
        xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    ASSERT_NE(keymap, nullptr) << "XKB keymap";
    // xkb_keymap_get_as_string() returns a malloc()ed string.
    const std::unique_ptr<char, decltype(&std::free)> text(
        xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1),
        &std::free);
    ASSERT_NE(text, nullptr) << "serialize keymap";
    const QJsonObject snapshot {
        {"event", "subscribe_keyboard"},
        {"keyboard_id", 1},
        {"name", "test"},
        {"depressed", 0},
        {"latched", 0},
        {"locked", 0},
        {"group", 0},
        {"rate", 0},
        {"delay", 0},
        {"keymap", QString::fromUtf8(text.get())},
    };
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    const QByteArray frame =
        QJsonDocument(snapshot).toJson(QJsonDocument::Compact) + '\n';
    f.peer->write(frame);
    f.peer->write("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":1,"
                  "\"keycode\":57,\"state\":\"pressed\"}\n");
    f.peer->flush();
    ASSERT_TRUE(waitFor([&] {
        f.log += f.resident.readAllStandardError();
        return f.log.contains("HELD 1");
    })) << "held key observed";
    write(f.config, base + "[symbols]\nspace='<space>'\n");
    ASSERT_TRUE(waitFor([&] {
        f.log += f.resident.readAllStandardError();
        return f.log.contains("HISTORY <space>");
    })) << "space-only TOML edit applies live to existing history";
    QProcess caller;
    f.command(caller);
    f.replacement();
    state(*f.peer, true);
    ASSERT_TRUE(waitFor([&] { return f.peer->canReadLine(); }))
        << "history restart restoration";
    ASSERT_EQ(f.peer->readLine(), "enable\n") << "history restart enable";
    state(*f.peer, false);
    ASSERT_EQ(result(caller), 0) << "history restart success";
    f.log.clear();
    f.log += f.resident.readAllStandardError();
    ASSERT_TRUE(f.log.contains("INITIAL 0 0"))
        << "fresh history and held-key state";
    f.peer->write(frame);
    f.peer->flush();
    tests::runEventLoopFor(200);
    f.log += f.resident.readAllStandardError();
    ASSERT_TRUE(!f.log.contains("HELD 1") && !f.log.contains("HISTORY <space>"))
        << "snapshot does not reconstruct old held keys/history";
}

TEST(OverlayRestart, WithoutAPluginRestartIsLocal) {
    Fixture f;
    QProcess absent;
    f.command(absent);
    ASSERT_TRUE(result(absent) == 1 && !QFile::exists(f.socket + ".overlay"))
        << "no resident restart does not start one";
    f.start(false, false);
    QProcess local;
    f.command(local);
    ASSERT_EQ(result(local), 0) << "local restart succeeds without plugin";
    ASSERT_TRUE(f.plugin.listen(f.socket)) << "late plugin listen";
    f.accept();
    state(*f.peer, true);
    tests::runEventLoopFor(400);
    ASSERT_EQ(f.peer->bytesAvailable(), 0)
        << "unavailable plugin restart has no pending enable";
}

// The overlay was updated but the compositor still runs the plugin from before:
// a first `toggle` says what to do, asks that plugin for nothing and leaves no
// resident behind.
TEST(OverlayRestart, IncompatiblePluginIsAskedForNothing) {
    Fixture f;
    // A plugin from before the hello existed starts with its capture state.
    f.greeting.clear();
    ASSERT_TRUE(f.plugin.listen(f.socket)) << "old plugin listen";
    f.launch(true);
    f.accept();
    state(*f.peer, true);
    ASSERT_TRUE(waitFor([&] {
        return f.resident.state() == QProcess::NotRunning;
    })) << "activation against an old plugin ends";
    const QByteArray output = f.resident.readAllStandardError();
    ASSERT_EQ(f.resident.exitCode(), 1)
        << "activation against an old plugin fails";
    ASSERT_TRUE(output.contains("Reload the plugin or restart Hyprland"))
        << "the failure says how to load the updated plugin";
    ASSERT_TRUE(f.peer->readAll().isEmpty())
        << "an old plugin is never asked to capture";
    ASSERT_FALSE(QFile::exists(f.socket + ".overlay"))
        << "no resident stays behind an old plugin";
}

TEST(OverlayRestart, FailedExecKeepsOwnership) {
    QTemporaryDir dir;
    const QString path = dir.filePath("event.sock");
    OverlayInstance owner(path);
    QString error;
    ASSERT_EQ(owner.acquire(OverlayInstance::Command::Start)
                  .value_or(OverlayInstance::Ownership::Forwarded),
              OverlayInstance::Ownership::Owner)
        << "fixture owner";
    QLocalSocket requester;
    requester.connectToServer(path + ".overlay");
    ASSERT_TRUE(requester.waitForConnected()) << "handoff requester connects";
    QObject::connect(&owner,
                     &OverlayInstance::restartRequested,
                     &owner,
                     [&](QLocalSocket* client) {
                         RestartContext context;
                         context.socketPath = path;
                         context.configPath = dir.filePath("config");
                         context.deadline = Clock::now() + RestartBudget;
                         error = execReplacement(
                             context,
                             owner.lockDescriptor(),
                             static_cast<int>(client->socketDescriptor()),
                             dir.filePath("not-an-executable"));
                         // Not ASSERT: the event loop calls this.
                         EXPECT_FALSE(error.isEmpty())
                             << "exec failure returns";
                         OverlayInstance::reply(client, error);
                     });
    requester.write("restart\n");
    requester.flush();
    ASSERT_TRUE(waitFor([&] { return requester.canReadLine(); }))
        << "exec failure response";
    ASSERT_TRUE(requester.readLine().contains("Executable replacement failed"))
        << "exec failure distinguished";
    OverlayInstance duplicate(path);
    ASSERT_FALSE(duplicate.acquire(OverlayInstance::Command::Start))
        << "failed exec retains ownership";
}

TEST(OverlayRestart, BadReplyNeverMeansSuccess) {
    const QTemporaryDir dir;
    const Fixture environment;
    for (const QByteArray& response :
         {QByteArray {}, QByteArray("ok\nextra\n")}) {
        const QString badPath = dir.filePath(
            response.isEmpty() ? "disconnected.sock" : "malformed.sock");
        QLocalServer bad;
        ASSERT_TRUE(bad.listen(badPath + ".overlay")) << "bad response fixture";
        // Hold the ownership lock as a resident would, so the caller forwards
        // here.
        const hyprcast::unique_fd lock {::open(
            (badPath + ".overlay.owner-lock").toLocal8Bit().constData(),
            O_RDWR | O_CREAT | O_CLOEXEC,
            0600)};
        ASSERT_TRUE(lock && ::flock(lock.get(), LOCK_EX | LOCK_NB) == 0)
            << "bad response fixture holds ownership";
        QObject::connect(&bad, &QLocalServer::newConnection, &bad, [&] {
            while (auto* client = bad.nextPendingConnection()) {
                QObject::connect(client,
                                 &QLocalSocket::disconnected,
                                 client,
                                 &QObject::deleteLater);
                QObject::connect(client,
                                 &QLocalSocket::readyRead,
                                 client,
                                 [client, response] {
                                     client->readAll();
                                     client->write(response);
                                     client->disconnectFromServer();
                                 });
            }
        });
        QProcess caller;
        caller.setProcessEnvironment(environment.environment);
        caller.start(executable(), {"restart", "--socket", badPath});
        ASSERT_TRUE(waitFor([&] {
            return caller.state() == QProcess::NotRunning;
        })) << "bad response caller exits boundedly";
        ASSERT_TRUE(
            caller.exitCode() == 1
            && caller.readAllStandardError().contains("outcome unknown"))
            << "disconnect/malformed reply never means success";
    }
}

} // namespace
int main(int argc, char** argv) {
    return tests::run<QCoreApplication>(argc, argv);
}
