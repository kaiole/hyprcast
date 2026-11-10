#include "Support.hpp"
#include "hyprcast/protocol/wire.hpp"
#include "ipc/CaptureController.hpp"
#include "ipc/IpcClient.hpp"
#include "ipc/OverlayInstance.hpp"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <memory>
#include <string_view>

using namespace hyprcast::overlay;

namespace {

using tests::waitFor;

// How this executable runs as another overlay process, see main().
constexpr std::string_view ForwardMode = "forward";
constexpr std::string_view CrashMode = "crash";

void state(QLocalSocket* peer, bool paused) {
    peer->write(paused ? "{\"event\":\"casting_state\",\"keyboard_id\":0,"
                         "\"paused\":true}\n"
                       : "{\"event\":\"casting_state\",\"keyboard_id\":0,"
                         "\"paused\":false}\n");
    peer->flush();
}
bool isOwner(const std::expected<OverlayInstance::Ownership, QString>& result) {
    return result && *result == OverlayInstance::Ownership::Owner;
}
// Starts a second overlay process that sends a toggle to the owner.
void startForwarder(QProcess& process, const QString& socket) {
    process.start(QCoreApplication::applicationFilePath(),
                  {QString::fromLatin1(ForwardMode), socket});
}
bool finished(const QProcess& process) {
    return process.state() == QProcess::NotRunning;
}

// The owning overlay instance and its capture controller, with a mock plugin
// listening on the socket.
class OverlayLifecycle : public testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(directory.isValid()) << "temporary directory";
        ASSERT_TRUE(isOwner(owner.acquire(OverlayInstance::Command::Start)))
            << "first instance owns lock";
        ASSERT_TRUE(plugin.listen(socket)) << "mock plugin listening";
        QObject::connect(&owner,
                         &OverlayInstance::toggleRequested,
                         &capture,
                         &CaptureController::toggle);
        QObject::connect(&capture,
                         &CaptureController::activeChanged,
                         &capture,
                         [this](bool value) {
                             active = value;
                             everActive = everActive || value;
                         });
        QObject::connect(&capture,
                         &CaptureController::commandFailed,
                         &capture,
                         [this](const QString& value) { failure = value; });
    }

    // The mock plugin's side of a new connection: the hello comes first.
    void acceptWithHello() {
        ASSERT_TRUE(waitFor([&] { return plugin.hasPendingConnections(); }))
            << "client connects";
        peer.reset(plugin.nextPendingConnection());
        peer->write(R"({"event":"hello","protocol_version":)"
                    + QByteArray::number(hyprcast::wire::protocol_version)
                    + "}\n");
    }
    // Connects the overlay to a plugin whose capture is paused.
    void connectPaused() {
        ipc.start();
        acceptWithHello();
        state(peer.get(), true);
    }
    // Brings capture up: the overlay asks for it and the plugin confirms.
    void enable() {
        capture.enableWhenReady();
        connectPaused();
        ASSERT_TRUE(waitFor([&] { return peer->canReadLine(); }))
            << "initial enable requested";
        ASSERT_EQ(peer->readLine(), "enable\n") << "explicit enable command";
        state(peer.get(), false);
        ASSERT_TRUE(waitFor([&] { return active; })) << "enable acknowledged";
    }

    QTemporaryDir directory;
    QString socket = directory.filePath("events.sock");
    OverlayInstance owner {socket};
    QLocalServer plugin;
    IpcClient ipc {socket};
    CaptureController capture {ipc};
    bool active = false;
    bool everActive = false;
    QString failure;
    std::unique_ptr<QLocalSocket> peer;
};

TEST_F(OverlayLifecycle, SecondStartIsRejected) {
    OverlayInstance duplicate(socket);
    EXPECT_FALSE(duplicate.acquire(OverlayInstance::Command::Start))
        << "plain duplicate rejected";
}

TEST_F(OverlayLifecycle, EnableIsActiveOnceThePluginConfirms) {
    capture.enableWhenReady();
    connectPaused();
    ASSERT_TRUE(waitFor([&] { return peer->canReadLine(); }))
        << "initial enable requested";
    EXPECT_EQ(peer->readLine(), "enable\n") << "explicit enable command";
    EXPECT_FALSE(active) << "not active until acknowledged";
    state(peer.get(), false);
    EXPECT_TRUE(waitFor([&] { return active; })) << "enable acknowledged";
}

TEST_F(OverlayLifecycle, ForwardedToggleDisablesThenEnables) {
    enable();

    QProcess off;
    startForwarder(off, socket);
    ASSERT_TRUE(waitFor([&] { return peer->canReadLine(); }))
        << "forwarded toggle reaches plugin";
    ASSERT_EQ(peer->readLine(), "disable\n") << "toggle requests disable";
    state(peer.get(), true);
    ASSERT_TRUE(waitFor([&] { return finished(off); }))
        << "disable caller receives reply";
    ASSERT_TRUE(off.exitCode() == 0 && !active)
        << "disable successful and hidden";

    QProcess on;
    startForwarder(on, socket);
    ASSERT_TRUE(waitFor([&] { return peer->canReadLine(); }))
        << "second toggle reaches plugin";
    ASSERT_EQ(peer->readLine(), "enable\n") << "second toggle enables";
    state(peer.get(), false);
    ASSERT_TRUE(waitFor([&] { return finished(on); }))
        << "enable caller receives reply";
    EXPECT_TRUE(on.exitCode() == 0 && active) << "enabled again";
}

TEST_F(OverlayLifecycle, ToggleDuringAPendingRequestIsRejected) {
    enable();
    QProcess off;
    startForwarder(off, socket);
    ASSERT_TRUE(waitFor([&] { return peer->canReadLine(); }))
        << "forwarded toggle reaches plugin";
    ASSERT_EQ(peer->readLine(), "disable\n") << "toggle requests disable";

    QProcess busy;
    startForwarder(busy, socket);
    ASSERT_TRUE(waitFor([&] { return finished(busy); }))
        << "concurrent request returns";
    EXPECT_EQ(busy.exitCode(), 1)
        << "busy request rejected rather than losing a toggle";

    state(peer.get(), true);
    ASSERT_TRUE(waitFor([&] { return finished(off); }))
        << "disable caller receives reply";
    EXPECT_TRUE(off.exitCode() == 0 && !active)
        << "disable successful and hidden";
}

TEST_F(OverlayLifecycle, ReconnectDoesNotEnableCaptureAgain) {
    enable();

    peer->abort();
    ASSERT_TRUE(waitFor([&] { return !active; })) << "disconnect hides overlay";
    acceptWithHello();
    state(peer.get(), true);
    ASSERT_TRUE(waitFor([&] { return ipc.hasCastingState(); }))
        << "reconnect snapshot received";
    QCoreApplication::processEvents();
    EXPECT_TRUE(!active && peer->bytesAvailable() == 0)
        << "reconnect does not re-enable capture";
}

TEST_F(OverlayLifecycle, UnconfirmedRequestTimesOut) {
    connectPaused();
    ASSERT_TRUE(waitFor([&] { return ipc.hasCastingState(); }))
        << "snapshot received";

    QProcess timeout;
    startForwarder(timeout, socket);
    ASSERT_TRUE(waitFor([&] { return peer->canReadLine(); }))
        << "unacknowledged request sent";
    peer->readAll();
    // The bound is what is tested: the caller must not wait forever.
    ASSERT_TRUE(waitFor([&] { return finished(timeout); }, 4500))
        << "request timeout bounded";
    EXPECT_TRUE(timeout.exitCode() == 1 && !failure.isEmpty())
        << "missing acknowledgement reported";
    EXPECT_FALSE(active) << "timeout stays hidden";
}

// The overlay was updated but the compositor still runs the plugin from before:
// requests fail at once with what to do, and that plugin is asked for nothing.
TEST_F(OverlayLifecycle, IncompatiblePluginIsAskedForNothing) {
    capture.enableWhenReady();
    ipc.start();
    ASSERT_TRUE(waitFor([&] { return plugin.hasPendingConnections(); }))
        << "client connects";
    // A plugin from before the hello existed starts with its capture state.
    peer.reset(plugin.nextPendingConnection());
    state(peer.get(), true);

    // Well inside the time a missing confirmation would take to report.
    ASSERT_TRUE(waitFor([&] { return !failure.isEmpty(); }, 1500))
        << "pending enable fails as soon as the plugin is refused";
    EXPECT_TRUE(failure.contains("Reload the plugin or restart Hyprland"))
        << "the failure says how to load the updated plugin";
    EXPECT_FALSE(capture.busy()) << "the refused request is over";

    QProcess toggle;
    startForwarder(toggle, socket);
    ASSERT_TRUE(waitFor([&] { return finished(toggle); }))
        << "toggle against an old plugin returns";
    EXPECT_EQ(toggle.exitCode(), 1) << "toggle against an old plugin fails";
    EXPECT_TRUE(toggle.readAllStandardError().contains(
        "Reload the plugin or restart Hyprland"))
        << "the toggle caller is told how to load the updated plugin";

    QCoreApplication::processEvents();
    EXPECT_TRUE(peer->readAll().isEmpty() && !everActive)
        << "an old plugin is never asked to capture";
}

// An instance on a socket path where no plugin listens.
class OverlayInstanceSocket : public testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(directory.isValid()) << "temporary directory";
    }

    QTemporaryDir directory;
    QString socket = directory.filePath("missing.sock");
    // Where the owning instance listens for the other overlay processes.
    QString instanceSocket = socket + ".overlay";
};

TEST_F(OverlayInstanceSocket, OwnerWithoutAPluginRefusesAToggleAndCleansUp) {
    {
        OverlayInstance owner(socket);
        ASSERT_TRUE(isOwner(owner.acquire(OverlayInstance::Command::Start)))
            << "owner without plugin";
        IpcClient ipc(socket);
        const CaptureController capture(ipc);
        QObject::connect(&owner,
                         &OverlayInstance::toggleRequested,
                         &capture,
                         &CaptureController::toggle);
        QProcess missing;
        startForwarder(missing, socket);
        ASSERT_TRUE(waitFor([&] { return finished(missing); }))
            << "unavailable reply";
        EXPECT_EQ(missing.exitCode(), 1) << "unavailable is not success";
    }
    EXPECT_FALSE(QFile::exists(instanceSocket)) << "socket removed on shutdown";
}

TEST_F(OverlayInstanceSocket, RecoversTheSocketOfACrashedOwner) {
    // A real stale Unix socket, left by a crashed process.
    QProcess crash;
    crash.start(QCoreApplication::applicationFilePath(),
                {QString::fromLatin1(CrashMode), socket});
    ASSERT_TRUE(crash.waitForFinished()) << "crash fixture exits";
    ASSERT_TRUE(QFile::exists(instanceSocket)) << "stale socket remains";

    OverlayInstance replacement(socket);
    EXPECT_TRUE(isOwner(replacement.acquire(OverlayInstance::Command::Start)))
        << "stale lock and socket recovered";
}

TEST_F(OverlayInstanceSocket, NeverUnlinksWhatIsNotASocket) {
    QFile obstruction(instanceSocket);
    ASSERT_TRUE(obstruction.open(QIODevice::WriteOnly))
        << "create non-socket obstruction";
    obstruction.close();

    OverlayInstance blocked(socket);
    EXPECT_FALSE(blocked.acquire(OverlayInstance::Command::Start))
        << "never unlink non-socket";
    EXPECT_TRUE(QFile::exists(obstruction.fileName()))
        << "obstruction preserved";
}

// Acts as another overlay process on `socket`: one that forwards a toggle to
// the owner, or an owner that dies without cleaning up.
int runAsOverlayProcess(std::string_view mode, const QString& socket) {
    OverlayInstance instance(socket);
    const bool crash = mode == CrashMode;
    const auto result =
        instance.acquire(crash ? OverlayInstance::Command::Start
                               : OverlayInstance::Command::Toggle);
    if (crash && isOwner(result)) {
        std::_Exit(0);
    }
    if (!result) {
        std::cerr << result.error().toStdString() << '\n';
    }
    return result && *result == OverlayInstance::Ownership::Forwarded ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    // The tests start this executable again as `<mode> <socket>`.
    if (argc == 3 && (argv[1] == ForwardMode || argv[1] == CrashMode)) {
        const QCoreApplication application(argc, argv);
        return runAsOverlayProcess(argv[1], QString::fromLocal8Bit(argv[2]));
    }
    return tests::run<QCoreApplication>(argc, argv);
}
