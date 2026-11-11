// Starts the overlay itself against a mock plugin and drives it through its
// command socket, as `hyprcast-overlay restart` and `toggle` do.
#include "Support.hpp"
#include "hyprcast/os/unique_fd.hpp"
#include "hyprcast/protocol/wire.hpp"

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
#include <QStringList>
#include <QTemporaryDir>
#include <cstdlib>
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

// How a caller ended: its exit code, or -1 if it did not answer in time, and
// what it printed.
struct Outcome {
    int code = -1;
    QByteArray error;
};
Outcome result(QProcess& process) {
    if (!waitFor([&] { return process.state() == QProcess::NotRunning; },
                 2 * tests::WaitTimeoutMs)) {
        ADD_FAILURE() << "bounded command reply";
        return {};
    }
    return {
        .code = process.exitCode(),
        .error = process.readAllStandardError(),
    };
}

// A theme that reports the window it is shown in: the output, whether the
// window lets clicks through, and when it is shown and hidden.
constexpr const char* WindowReporter =
    "import QtQuick\n"
    "Item {\n"
    "    readonly property var win: Window.window\n"
    "    readonly property string report: win ? Screen.name + ' ' + ((win.flags"
    " & Qt.WindowTransparentForInput) !== 0) : ''\n"
    "    onReportChanged: if (report) console.warn('WINDOW', report)\n"
    "    readonly property bool shown: win ? win.visible : false\n"
    "    onShownChanged: console.warn('SHOWN', shown)\n"
    "}\n";

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
        write(config, "[window]\nwidth = 610\n");
    }
    // Installs a theme whose only file besides the manifest is `main`.
    void install(const QString& id, const QByteArray& main) const {
        const QString theme = themePath(id);
        ASSERT_TRUE(QDir().mkpath(theme)) << "theme package directory";
        write(theme + "/theme.toml",
              "[theme]\nid = '" + id.toUtf8()
                  + "'\nname = 'Test'\n"
                    "api_version = 1\nentry = 'Main.qml'\n");
        write(theme + "/Main.qml", main);
    }
    [[nodiscard]] QString themePath(const QString& id) const {
        return directory.filePath("hyprcast/themes/" + id);
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
        ASSERT_TRUE(logged("777x"))
            << "startup with original explicit width override";
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
    // Runs `hyprcast-overlay restart` to its end.
    Outcome restart(const QStringList& extra = {}) {
        QProcess caller;
        command(caller, extra);
        return result(caller);
    }
    // Runs `hyprcast-overlay toggle`, answering it as the plugin would.
    void toggle(bool enable) {
        QProcess caller;
        command(caller, {}, "toggle");
        ASSERT_TRUE(waitFor([&] { return peer->canReadLine(); }))
            << "capture request";
        ASSERT_EQ(peer->readLine(), enable ? "enable\n" : "disable\n")
            << "capture request follows the state the overlay holds";
        state(*peer, !enable);
        ASSERT_EQ(result(caller).code, 0) << "capture change confirmed";
    }
    // Waits until the overlay has logged `text` `times` times.
    [[nodiscard]] bool logged(const QByteArray& text, qsizetype times = 1) {
        return waitFor([&] {
            log += resident.readAllStandardError();
            return log.count(text) >= times;
        });
    }
    // Lets the overlay run and reports whether it is as it was: the same
    // process, still connected, having asked the plugin for nothing.
    [[nodiscard]] bool undisturbed(qint64 pid) {
        tests::runEventLoopFor(200);
        log += resident.readAllStandardError();
        return resident.state() == QProcess::Running
            && resident.processId() == pid
            && peer->state() == QLocalSocket::ConnectedState
            && peer->bytesAvailable() == 0;
    }
};

TEST(OverlayQuit, EndsTheResidentAndItsPluginConnection) {
    Fixture f;
    f.start(true);

    QProcess caller;
    f.command(caller, {}, "quit");
    ASSERT_EQ(result(caller).code, 0) << "quit is confirmed";
    ASSERT_TRUE(waitFor([&] {
        return f.resident.state() == QProcess::NotRunning;
    })) << "the resident ends";
    EXPECT_EQ(f.resident.exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(f.resident.exitCode(), 0) << "an asked-for end is a success";
    ASSERT_TRUE(waitFor([&] {
        return f.peer->state() == QLocalSocket::UnconnectedState;
    })) << "the plugin connection closes, which pauses capture";

    QProcess next;
    next.setProcessEnvironment(f.environment);
    next.start(
        executable(),
        {"--socket", f.socket, "--config", f.config, "--quit-after-ms", "1"});
    ASSERT_TRUE(next.waitForStarted()) << "start a new resident";
    ASSERT_TRUE(waitFor([&] { return f.plugin.hasPendingConnections(); }))
        << "a new overlay can take the session";
    next.kill();
    next.waitForFinished();
}

TEST(OverlayQuit, SucceedsWithNoResident) {
    Fixture f;
    QProcess caller;
    f.command(caller, {}, "quit");
    const Outcome outcome = result(caller);
    EXPECT_EQ(outcome.code, 0) << "nothing to end is not a failure";
    EXPECT_TRUE(outcome.error.isEmpty()) << outcome.error.constData();
}

TEST(OverlayQuit, RefusesStartupOverrides) {
    Fixture f;
    f.start(true);
    const auto pid = f.resident.processId();
    QProcess caller;
    f.command(caller, {"--width", "10"}, "quit");
    EXPECT_EQ(result(caller).code, 2) << "an override is a usage error";
    ASSERT_TRUE(f.undisturbed(pid)) << "the resident keeps running";
}

TEST(OverlayReload, KeepsCaptureAndTheConnection) {
    Fixture f;
    f.install("reporter", WindowReporter);
    write(f.config, "[theme]\nid = 'reporter'\n");
    f.start(true);
    const auto pid = f.resident.processId();
    ASSERT_TRUE(f.logged("SHOWN true")) << "capture shows the window";

    ASSERT_EQ(f.restart().code, 0) << "restart of a capturing overlay";
    ASSERT_TRUE(f.undisturbed(pid))
        << "a restart keeps the process and the plugin connection, and asks "
           "the plugin for nothing";
    ASSERT_TRUE(f.logged("SHOWN true", 2))
        << "the rebuilt window is shown, as capture is still on";
    ASSERT_TRUE(f.logged("Overlay on", 2)) << "the window was rebuilt";

    QProcess second;
    second.setProcessEnvironment(f.environment);
    second.start(executable(), {"--socket", f.socket});
    ASSERT_EQ(result(second).code, 1) << "the resident still owns the session";

    f.toggle(false);
    ASSERT_TRUE(f.logged("SHOWN false", 2))
        << "the rebuilt window follows the capture state";
}

TEST(OverlayReload, SavedConfigAppliesToTheRebuiltWindow) {
    Fixture f;
    f.install("reporter", WindowReporter);
    f.start(true);
    ASSERT_EQ(f.restart().code, 0) << "restart on the bundled theme";
    write(f.config, "[theme]\nid = 'reporter'\n");
    ASSERT_TRUE(f.logged("SHOWN true"))
        << "a theme selected on save is loaded into the rebuilt window";
}

TEST(OverlayReload, PausedOverlayStaysHidden) {
    Fixture f;
    f.install("reporter", WindowReporter);
    write(f.config, "[theme]\nid = 'reporter'\n");
    f.start();
    const auto pid = f.resident.processId();
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(f.restart().code, 0) << "restart of a paused overlay";
        ASSERT_TRUE(f.undisturbed(pid)) << "a paused restart never enables";
    }
    ASSERT_TRUE(f.logged("Overlay on", 4)) << "each restart rebuilt the window";
    ASSERT_FALSE(f.log.contains("SHOWN true"))
        << "a paused overlay is never shown";
    f.toggle(true);
    ASSERT_TRUE(f.logged("SHOWN true")) << "capture shows the rebuilt window";
}

TEST(OverlayReload, RepeatedRestartsLeakNoDescriptors) {
    Fixture f;
    f.start(true);
    const auto pid = f.resident.processId();
    const auto openDescriptors = [pid] {
        return QDir(QStringLiteral("/proc/%1/fd").arg(pid))
            .entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
            .size();
    };
    ASSERT_EQ(f.restart().code, 0) << "first restart";
    ASSERT_TRUE(f.undisturbed(pid)) << "settle after the first restart";
    // Counted once the window has been rebuilt, as it is from then on.
    const qsizetype baseline = openDescriptors();
    ASSERT_GT(baseline, 0) << "the descriptors of the overlay are readable";
    for (int i = 0; i < 4; ++i) {
        ASSERT_EQ(f.restart().code, 0) << "repeated restart";
    }
    ASSERT_TRUE(f.undisturbed(pid)) << "settle after the last restart";
    ASSERT_LE(openDescriptors(), baseline)
        << "repeated restarts do not leak descriptors";
}

TEST(OverlayReload, PendingFirstEnableSurvives) {
    Fixture f;
    f.start(true, true, false);
    const auto pid = f.resident.processId();
    ASSERT_EQ(f.restart().code, 0)
        << "a restart does not wait for the plugin to confirm capture";
    ASSERT_TRUE(f.undisturbed(pid))
        << "the enable already sent is not repeated";
    state(*f.peer, false);
    f.toggle(false);
}

// A first `toggle` that the plugin never confirms ends the overlay, restarted
// in between or not. This is also the one test whose overlay exits by itself:
// under the sanitizers, that checks what a restart left behind.
TEST(OverlayReload, UnconfirmedFirstEnableStillEndsTheOverlay) {
    Fixture f;
    f.start(true, true, false);
    ASSERT_EQ(f.restart().code, 0) << "restart before capture is confirmed";
    ASSERT_TRUE(waitFor([&] {
        return f.resident.state() == QProcess::NotRunning;
    })) << "the unconfirmed activation ends the overlay";
    ASSERT_TRUE(f.resident.exitStatus() == QProcess::NormalExit
                && f.resident.exitCode() == 1)
        << "it exits with a failure, not a crash or a leak report";
    ASSERT_TRUE(
        f.resident.readAllStandardError().contains("did not confirm capture"))
        << "the failure says what was missing";
}

TEST(OverlayReload, BadConfigIsRefusedUntouched) {
    Fixture f;
    f.start(true);
    const auto pid = f.resident.processId();
    write(f.config, "[window]\nwidth = 'invalid'\n");
    const Outcome invalid = f.restart();
    ASSERT_EQ(invalid.code, 1) << "an invalid config fails the restart";
    ASSERT_TRUE(invalid.error.contains("overlay.toml"))
        << "the failure names the config file";
    ASSERT_TRUE(f.undisturbed(pid)) << "the running overlay is left alone";
    QFile::remove(f.config);
    ASSERT_EQ(f.restart().code, 1) << "a missing required config fails too";
    ASSERT_TRUE(f.undisturbed(pid)) << "the running overlay is left alone";
    ASSERT_EQ(f.log.count("Overlay on"), 1)
        << "a refused restart rebuilds nothing";
}

TEST(OverlayReload, LoadsFreshThemeContent) {
    Fixture f;
    f.start();
    const auto pid = f.resident.processId();
    const QString theme = f.themePath("fresh");
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
    write(f.config, "[window]\nwidth = 620\n[theme]\nid = 'fresh'\n");
    ASSERT_EQ(f.restart().code, 0) << "a newly installed theme is found";
    ASSERT_TRUE(f.logged("FRESH-one helper-one one") && f.logged("theme=fresh")
                && f.logged("ASSET 1"))
        << "the new theme is shown";
    ASSERT_TRUE(f.logged("777x", 2))
        << "the overrides the overlay was started with outlive a restart";

    // The same ID and paths: new QML, helper, manifest default and image.
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
    ASSERT_EQ(f.restart().code, 0) << "restart after editing the theme";
    ASSERT_TRUE(f.logged("FRESH-two helper-two two"))
        << "edited QML, an edited helper and an edited manifest are read "
           "again";
    ASSERT_TRUE(f.logged("ASSET 2"))
        << "an image replaced at the same path is read again, although Qt "
           "caches images for the whole process";
    ASSERT_TRUE(f.undisturbed(pid)) << "all of it in the running overlay";
}

TEST(OverlayReload, BrokenThemeFallsBackToTheBundledOne) {
    Fixture f;
    f.install("fragile",
              "import QtQuick\nItem { Component.onCompleted: "
              "console.warn('GOOD-one') }\n");
    write(f.config, "[theme]\nid = 'fragile'\n");
    f.start(true);
    const auto pid = f.resident.processId();
    ASSERT_TRUE(f.logged("theme=fragile") && f.logged("GOOD-one"))
        << "the theme loads at first";

    write(f.themePath("fragile") + "/Main.qml", "not valid QML\n");
    const Outcome broken = f.restart();
    ASSERT_EQ(broken.code, 1) << "a theme that does not load fails the restart";
    ASSERT_TRUE(broken.error.contains("fragile")
                && broken.error.contains("Main.qml")
                && broken.error.contains("built-in theme"))
        << "the caller is told which theme failed, where, and what is shown "
           "instead";
    ASSERT_TRUE(f.logged("theme=builtin:default"))
        << "the bundled theme is shown in its place";
    ASSERT_TRUE(f.undisturbed(pid))
        << "the overlay runs on, connected, and asks the plugin for nothing";

    // The configuration still names the broken theme: its other settings
    // keep applying on save.
    write(f.config, "[theme]\nid = 'fragile'\n[appearance]\nfont_size = 31\n");
    tests::runEventLoopFor(600);
    f.log += f.resident.readAllStandardError();
    ASSERT_TRUE(!f.log.contains("keeping the last valid configuration")
                && !f.log.contains("no matching theme instance"))
        << "a saved configuration is still accepted and applied";

    f.install("fragile",
              "import QtQuick\nItem { Component.onCompleted: "
              "console.warn('GOOD-two') }\n");
    ASSERT_EQ(f.restart().code, 0) << "restart with the theme repaired";
    ASSERT_TRUE(f.logged("theme=fragile", 2) && f.logged("GOOD-two"))
        << "the repaired theme is shown again";
    f.toggle(false);
}

TEST(OverlayReload, MisuseAndUnavailableOutputAreRejected) {
    Fixture f;
    f.start();
    const auto pid = f.resident.processId();
    ASSERT_EQ(f.restart({"--width", "999"}).code, 2)
        << "a restart takes no overrides";
    ASSERT_EQ(f.restart({"--not-an-option"}).code, 2)
        << "an unknown restart option is CLI misuse";
    write(f.config, "[window]\nmonitor='not-an-output'\n");
    const Outcome monitor = f.restart();
    ASSERT_EQ(monitor.code, 1) << "an output that is not connected is refused";
    ASSERT_TRUE(monitor.error.contains("not-an-output"))
        << "the failure names the output";
    ASSERT_TRUE(f.undisturbed(pid)) << "the running overlay is left alone";
    ASSERT_EQ(f.log.count("Overlay on"), 1)
        << "a refused restart rebuilds nothing";
}

TEST(OverlayReload, WindowOptionsApplyOnSave) {
    Fixture f;
    const QString screens = f.directory.filePath("screens.json");
    write(screens,
          R"({"screens":[)"
          R"({"name":"first","x":0,"y":0,"width":800,"height":600},)"
          R"({"name":"second","x":800,"y":0,"width":640,"height":480}]})"
          "\n");
    f.environment.insert("QT_QPA_PLATFORM", "offscreen:configfile=" + screens);
    f.install("reporter", WindowReporter);
    const QByteArray base = "[theme]\nid = 'reporter'\n";
    write(f.config, base);
    f.start(true);
    const auto pid = f.resident.processId();
    ASSERT_TRUE(f.logged("WINDOW first true"))
        << "by default on the primary output, letting clicks through";

    write(f.config, base + "[window]\nmonitor = 'second'\n");
    ASSERT_TRUE(f.logged("WINDOW second true") && f.logged("Overlay on second"))
        << "a saved window.monitor moves the overlay without a restart";

    const QByteArray taken =
        base + "[window]\nmonitor = 'second'\nclick_through = false\n";
    write(f.config, taken);
    ASSERT_TRUE(f.logged("WINDOW second false"))
        << "a saved window.click_through is applied without a restart";
    ASSERT_TRUE(f.logged("SHOWN true", 3) && f.undisturbed(pid))
        << "each window is shown, in the same connected process";

    write(f.config, base + "[window]\nmonitor = 'not-an-output'\n");
    ASSERT_TRUE(f.logged("keeping the last valid configuration")
                && f.log.contains("not-an-output"))
        << "an output that is not connected is refused on save";
    ASSERT_TRUE(f.undisturbed(pid) && f.log.count("Overlay on") == 3)
        << "and the window stays where it is";

    write(f.config, taken);
    ASSERT_EQ(f.restart().code, 0) << "restart with window options saved";
    ASSERT_TRUE(f.logged("WINDOW second false", 2))
        << "a restart builds its window with the saved options";
    ASSERT_TRUE(f.logged("SHOWN true", 4) && f.undisturbed(pid))
        << "shown again, in the same connected process";
}

TEST(OverlayReload, KeepsHistoryAndHeldKeys) {
    Fixture f;
    f.install(
        "input-test",
        "import QtQuick\nItem { Component.onCompleted: console.warn('INITIAL', "
        "hyprcast.historyCount, hyprcast.heldKeyCount, "
        "hyprcast.displayHistory.displayText); Connections { target: "
        "hyprcast.displayHistory; function onDisplayTextChanged() { "
        "console.warn('HISTORY', hyprcast.displayHistory.displayText) } } "
        "Connections { "
        "target: hyprcast; function onHeldKeysChanged() { console.warn('HELD', "
        "hyprcast.heldKeyCount) } } }\n");
    write(f.config,
          "[theme]\nid='input-test'\n[expiration]\nafter_ms=0\n"
          "[symbols]\nspace='<space>'\n");
    f.start(true);
    const auto pid = f.resident.processId();
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
    f.peer->write(QJsonDocument(snapshot).toJson(QJsonDocument::Compact)
                  + '\n');
    f.peer->write("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":1,"
                  "\"keycode\":57,\"state\":\"pressed\"}\n");
    f.peer->flush();
    ASSERT_TRUE(f.logged("HELD 1") && f.logged("HISTORY <space>"))
        << "a held key and its history entry are shown";

    ASSERT_EQ(f.restart().code, 0) << "restart with a key held";
    ASSERT_TRUE(f.logged("INITIAL 1 1 <space>"))
        << "the rebuilt theme starts from the history and held keys of the "
           "connection, which is still the same";
    ASSERT_TRUE(f.undisturbed(pid)) << "the plugin is asked for nothing";

    // The keymap came with the connection and is never sent again.
    f.peer->write("{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":2,"
                  "\"keycode\":57,\"state\":\"released\"}\n"
                  "{\"event\":\"key\",\"keyboard_id\":1,\"time_ms\":3,"
                  "\"keycode\":30,\"state\":\"pressed\"}\n");
    f.peer->flush();
    ASSERT_TRUE(f.logged("HISTORY <space>a"))
        << "later keys are interpreted with the keymap received before the "
           "restart";
}

TEST(OverlayReload, WithoutAPluginRestartIsLocal) {
    Fixture f;
    ASSERT_TRUE(f.restart().code == 1 && !QFile::exists(f.socket + ".overlay"))
        << "a restart without a resident does not start one";
    f.start(false, false);
    ASSERT_EQ(f.restart().code, 0) << "a restart needs no plugin";
    ASSERT_TRUE(f.plugin.listen(f.socket)) << "late plugin listen";
    f.accept();
    state(*f.peer, true);
    tests::runEventLoopFor(400);
    ASSERT_EQ(f.peer->bytesAvailable(), 0)
        << "a restart leaves no enable pending for a plugin that comes later";
}

TEST(OverlayReload, SignatureSelectsTheSession) {
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
    ASSERT_EQ(result(caller).code, 0)
        << "the signature routes the restart to the selected session";
    ASSERT_TRUE(selected.logged("Overlay on", 2))
        << "the selected overlay rebuilt its window";
    ASSERT_EQ(
        selected.restart({"--instance-signature", "invalid/session"}).code,
        0)
        << "an explicit socket takes precedence over the signature";
    ASSERT_TRUE(selected.logged("Overlay on", 3))
        << "the selected overlay rebuilt its window again";
    tests::runEventLoopFor(200);
    other.log += other.resident.readAllStandardError();
    ASSERT_EQ(other.log.count("Overlay on"), 1)
        << "a restart never reaches another session";
}

// The overlay was updated but the compositor still runs the plugin from before:
// a first `toggle` says what to do, asks that plugin for nothing and leaves no
// resident behind.
TEST(OverlayStartup, IncompatiblePluginIsAskedForNothing) {
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

TEST(OverlayCommand, BadReplyNeverMeansSuccess) {
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
