#include "ipc/CaptureController.hpp"
#include "ipc/OverlayInstance.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>

using namespace Hyprcast::Overlay;

namespace {
    void check(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "check failed: " << message << '\n';
            std::exit(1);
        }
    }
    bool until(const std::function<bool()>& predicate, int timeout = 2000) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeout) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        return predicate();
    }
    void state(QLocalSocket* peer, bool paused) {
        peer->write(paused ? "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":true}\n" : "{\"event\":\"casting_state\",\"keyboard_id\":0,\"paused\":false}\n");
        peer->flush();
    }
    bool isOwner(const std::expected<OverlayInstance::Ownership, QString>& result) {
        return result && *result == OverlayInstance::Ownership::Owner;
    }
    void startForwarder(QProcess& process, const QString& socket) {
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("forward"), socket});
    }
    void testLifecycle() {
        QTemporaryDir directory;
        check(directory.isValid(), "temporary directory");
        const QString   socket = directory.filePath("events.sock");
        OverlayInstance owner(socket);
        check(isOwner(owner.acquire(OverlayInstance::Command::Start)), "first instance owns lock");
        OverlayInstance duplicate(socket);
        check(!duplicate.acquire(OverlayInstance::Command::Start), "plain duplicate rejected");

        QLocalServer plugin;
        check(plugin.listen(socket), "mock plugin listening");
        IpcClient         ipc(socket);
        CaptureController capture(ipc);
        QObject::connect(&owner, &OverlayInstance::toggleRequested, &capture, &CaptureController::toggle);
        bool    active = false;
        QString failure;
        QObject::connect(&capture, &CaptureController::activeChanged, &capture, [&](bool value) { active = value; });
        QObject::connect(&capture, &CaptureController::commandFailed, &capture, [&](const QString& value) { failure = value; });
        capture.enableWhenReady();
        ipc.start();
        check(until([&] { return plugin.hasPendingConnections(); }), "client connects");
        std::unique_ptr<QLocalSocket> peer(plugin.nextPendingConnection());
        state(peer.get(), true);
        check(until([&] { return peer->canReadLine(); }), "initial enable requested");
        check(peer->readLine() == "enable\n", "explicit enable command");
        check(!active, "not active until acknowledged");
        state(peer.get(), false);
        check(until([&] { return active; }), "enable acknowledged");

        QProcess off;
        startForwarder(off, socket);
        check(until([&] { return peer->canReadLine(); }), "forwarded toggle reaches plugin");
        check(peer->readLine() == "disable\n", "toggle requests disable");
        QProcess busy;
        startForwarder(busy, socket);
        check(until([&] { return busy.state() == QProcess::NotRunning; }), "concurrent request returns");
        check(busy.exitCode() == 1, "busy request rejected rather than losing a toggle");
        state(peer.get(), true);
        check(until([&] { return off.state() == QProcess::NotRunning; }), "disable caller receives reply");
        check(off.exitCode() == 0 && !active, "disable successful and hidden");

        QProcess on;
        startForwarder(on, socket);
        check(until([&] { return peer->canReadLine(); }), "second toggle reaches plugin");
        check(peer->readLine() == "enable\n", "second toggle enables");
        state(peer.get(), false);
        check(until([&] { return on.state() == QProcess::NotRunning; }), "enable caller receives reply");
        check(on.exitCode() == 0 && active, "enabled again");
        peer->abort();
        check(until([&] { return !active; }), "disconnect hides overlay");
        check(until([&] { return plugin.hasPendingConnections(); }), "reconnect attempted");
        peer.reset(plugin.nextPendingConnection());
        state(peer.get(), true);
        check(until([&] { return ipc.hasCastingState(); }), "reconnect snapshot received");
        QCoreApplication::processEvents();
        check(!active && !peer->bytesAvailable(), "reconnect does not re-enable capture");

        QProcess timeout;
        startForwarder(timeout, socket);
        check(until([&] { return peer->canReadLine(); }), "unacknowledged request sent");
        peer->readAll();
        check(until([&] { return timeout.state() == QProcess::NotRunning; }, 4500), "request timeout bounded");
        check(timeout.exitCode() == 1 && !failure.isEmpty(), "missing acknowledgement reported");
        check(!active, "timeout stays hidden");
    }

    void testUnavailableAndCleanup() {
        QTemporaryDir directory;
        const QString socket = directory.filePath("missing.sock");
        {
            OverlayInstance owner(socket);
            check(isOwner(owner.acquire(OverlayInstance::Command::Start)), "owner without plugin");
            IpcClient         ipc(socket);
            CaptureController capture(ipc);
            QObject::connect(&owner, &OverlayInstance::toggleRequested, &capture, &CaptureController::toggle);
            QProcess missing;
            startForwarder(missing, socket);
            check(until([&] { return missing.state() == QProcess::NotRunning; }), "unavailable reply");
            check(missing.exitCode() == 1, "unavailable is not success");
        }
        check(!QFile::exists(socket + ".overlay"), "socket removed on shutdown");
        // A real stale Unix socket, left by a crashed process.
        QProcess crash;
        crash.start(QCoreApplication::applicationFilePath(), {QStringLiteral("crash"), socket});
        check(crash.waitForFinished(), "crash fixture exits");
        check(QFile::exists(socket + ".overlay"), "stale socket remains");
        {
            OverlayInstance replacement(socket);
            check(isOwner(replacement.acquire(OverlayInstance::Command::Start)), "stale lock and socket recovered");
        }
        QFile obstruction(socket + ".overlay");
        check(obstruction.open(QIODevice::WriteOnly), "create non-socket obstruction");
        obstruction.close();
        OverlayInstance blocked(socket);
        check(!blocked.acquire(OverlayInstance::Command::Start), "never unlink non-socket");
        check(QFile::exists(obstruction.fileName()), "obstruction preserved");
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3) {
        OverlayInstance instance(app.arguments().at(2));
        const bool      crash  = app.arguments().at(1) == "crash";
        const auto      result = instance.acquire(crash ? OverlayInstance::Command::Start : OverlayInstance::Command::Toggle);
        if (crash && isOwner(result)) {
            std::_Exit(0);
        }
        return result && *result == OverlayInstance::Ownership::Forwarded ? 0 : 1;
    }
    testLifecycle();
    testUnavailableAndCleanup();
    return 0;
}
