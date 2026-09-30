#include "OverlayInstance.hpp"
#include "Restart.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalSocket>
#include <QLockFile>
#include <QThread>
#include <QTimer>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Hyprcast::Overlay {
    namespace {
        bool liveServer(const QString& path) {
            QLocalSocket probe;
            probe.connectToServer(path);
            return probe.waitForConnected(100);
        }
    }
    OverlayInstance::OverlayInstance(const QString& eventSocket, QObject* parent) :
        QObject(parent), m_path(QFileInfo(eventSocket).absoluteFilePath() + QStringLiteral(".overlay")) {
        m_server.setSocketOptions(QLocalServer::UserAccessOption);
        m_server.setMaxPendingConnections(16);
        connect(&m_server, &QLocalServer::newConnection, this, [this] {
            while (auto* client = m_server.nextPendingConnection()) {
                client->setReadBufferSize(32);
                connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                QTimer::singleShot(RestartReplyMs + 1000, client, [client] { client->abort(); client->deleteLater(); });
                connect(client, &QLocalSocket::readyRead, this, [this, client] {
                    if (client->property("handled").toBool()) return;
                    if (!client->canReadLine() && client->bytesAvailable() < 32) return;
                    client->setProperty("handled", true);
                    const QByteArray request = client->readAll();
                    if (request == "toggle\n") emit toggleRequested(client);
                    else if (request == "restart\n") emit restartRequested(client);
                    else reply(client, QStringLiteral("Invalid overlay command"));
                });
            }
        });
    }
    OverlayInstance::~OverlayInstance() {
        m_server.close();
        if (m_lockFd >= 0) ::close(m_lockFd);
    }
    OverlayInstance::Result OverlayInstance::acquire(Command command, QString* error, int inheritedLock) {
        const QString lockPath = m_path + QStringLiteral(".owner-lock");
        if (inheritedLock >= 0) {
            struct stat fdInfo{}, pathInfo{};
            if (::fstat(inheritedLock, &fdInfo) || ::lstat(lockPath.toLocal8Bit().constData(), &pathInfo) || !S_ISREG(fdInfo.st_mode) ||
                fdInfo.st_uid != ::getuid() || fdInfo.st_dev != pathInfo.st_dev || fdInfo.st_ino != pathInfo.st_ino ||
                ::flock(inheritedLock, LOCK_EX | LOCK_NB)) {
                *error = QStringLiteral("Cannot adopt restart ownership lock");
                return Result::Error;
            }
            m_lockFd = inheritedLock;
        } else {
            if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
                *error = QStringLiteral("Cannot create overlay socket directory");
                return Result::Error;
            }
            m_lockFd = ::open(lockPath.toLocal8Bit().constData(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
            struct stat info{};
            if (m_lockFd < 0 || ::fstat(m_lockFd, &info) || !S_ISREG(info.st_mode) || info.st_uid != ::getuid() || (info.st_mode & 0077)) {
                *error = QStringLiteral("Cannot safely open overlay ownership lock");
                return Result::Error;
            }
            const bool locked = ::flock(m_lockFd, LOCK_EX | LOCK_NB) == 0;
            // A pre-feature owner uses QLockFile, not flock. Never unlink its live server.
            const bool live = locked && liveServer(m_path);
            if (!locked || live || command == Command::Restart) {
                if (locked) { ::close(m_lockFd); m_lockFd = -1; }
                if (command == Command::Start) {
                    *error = QStringLiteral("An overlay is already running for this session; use 'toggle'");
                    return Result::Error;
                }
                QLocalSocket client;
                QElapsedTimer deadline;
                deadline.start();
                do {
                    client.abort();
                    client.connectToServer(m_path);
                    if (client.waitForConnected(100)) break;
                    QThread::msleep(25);
                } while (deadline.elapsed() < 3000);
                if (client.state() != QLocalSocket::ConnectedState) {
                    *error = locked && !live ? QStringLiteral("No overlay is running for this session; use toggle to start it.") :
                                              QStringLiteral("Overlay is starting or unresponsive; try again");
                    return Result::Error;
                }
                client.write(command == Command::Restart ? "restart\n" : "toggle\n");
                client.flush();
                QByteArray response;
                deadline.restart();
                const int allowance = command == Command::Restart ? RestartReplyMs : 4000;
                while (!response.contains('\n') && response.size() < 1024 && deadline.elapsed() < allowance) {
                    if (!client.bytesAvailable() && !client.waitForReadyRead(allowance - int(deadline.elapsed()))) break;
                    response += client.readAll();
                }
                if (response == "ok\n") return Result::Forwarded;
                if (command == Command::Restart && response == "error: Invalid overlay command\n") {
                    *error = QStringLiteral("Resident does not support restart; manually close/relaunch the overlay once after upgrading");
                    return Result::Error;
                }
                *error = response.startsWith("error: ") && response.endsWith('\n') && response.size() <= 1024 ? QString::fromUtf8(response).trimmed() :
                         QStringLiteral("Overlay did not confirm completion; outcome unknown. Do not automatically retry.");
                return Result::Error;
            }
        }
        // Also guard against an older binary still starting. Its lock is held until
        // our socket is listening; a live old owner is never treated as stale.
        QLockFile legacy(m_path + QStringLiteral(".lock"));
        legacy.setStaleLockTime(0);
        if (!legacy.tryLock()) {
            *error = QStringLiteral("Legacy overlay is starting or running; relaunch it manually once to support restart");
            return Result::Error;
        }
        struct stat info{};
        const auto path = m_path.toLocal8Bit();
        if (::lstat(path.constData(), &info) == 0) {
            if (!S_ISSOCK(info.st_mode)) {
                *error = QStringLiteral("Refusing to remove non-socket entry: %1").arg(m_path);
                return Result::Error;
            }
            if (!QLocalServer::removeServer(m_path)) {
                *error = QStringLiteral("Cannot remove stale overlay socket");
                return Result::Error;
            }
        } else if (errno != ENOENT) {
            *error = QStringLiteral("Cannot inspect overlay socket");
            return Result::Error;
        }
        if (!m_server.listen(m_path)) { *error = m_server.errorString(); return Result::Error; }
        return Result::Owner;
    }
    void OverlayInstance::reply(QLocalSocket* client, const QString& error) {
        if (!client) return;
        QString clean = error;
        clean.replace('\n', ' ');
        clean.replace('\r', ' ');
        client->write(error.isEmpty() ? QByteArrayLiteral("ok\n") : QByteArray("error: ") + clean.toUtf8().left(1000) + '\n');
        client->disconnectFromServer();
    }
}
