#include "OverlayInstance.hpp"
#include "Restart.hpp"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QLockFile>
#include <QThread>
#include <QTimer>

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Hyprcast::Overlay {
    namespace {
        using namespace std::chrono_literals;

        constexpr qsizetype MaxCommandBytes = 32;
        constexpr qsizetype MaxReplyBytes   = 1024;
        constexpr int       MaxPending      = 16;
        constexpr auto      ProbeTimeout    = 100ms;
        constexpr auto      ConnectRetry    = 25ms;
        constexpr auto      ConnectBudget   = 3s;
        constexpr auto      ToggleReplyWait = 4s;

        bool                liveServer(const QString& path) {
            QLocalSocket probe;
            probe.connectToServer(path);
            return probe.waitForConnected(static_cast<int>(ProbeTimeout.count()));
        }
    }

    OverlayInstance::OverlayInstance(const QString& eventSocket, QObject* parent) :
        QObject(parent), m_path(QFileInfo(eventSocket).absoluteFilePath() + QStringLiteral(".overlay")) {
        m_server.setSocketOptions(QLocalServer::UserAccessOption);
        m_server.setMaxPendingConnections(MaxPending);
        connect(&m_server, &QLocalServer::newConnection, this, [this] {
            while (auto* client = m_server.nextPendingConnection()) {
                client->setReadBufferSize(MaxCommandBytes);
                connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                QTimer::singleShot(RestartReplyTimeout + 1s, client, [client] {
                    client->abort();
                    client->deleteLater();
                });
                connect(client, &QLocalSocket::readyRead, this, [this, client] {
                    if (!client->canReadLine() && client->bytesAvailable() < MaxCommandBytes) {
                        return;
                    }
                    // One command per connection; ignore anything that follows.
                    disconnect(client, &QLocalSocket::readyRead, this, nullptr);
                    const QByteArray request = client->readAll();
                    if (request == "toggle\n") {
                        emit toggleRequested(client);
                    } else if (request == "restart\n") {
                        emit restartRequested(client);
                    } else {
                        reply(client, QStringLiteral("Invalid overlay command"));
                    }
                });
            }
        });
    }

    std::expected<OverlayInstance::Ownership, QString> OverlayInstance::acquire(Command command, CUniqueFd inheritedLock) {
        const QString lockPath = m_path + QStringLiteral(".owner-lock");
        if (inheritedLock) {
            if (auto adopted = adoptLock(lockPath, std::move(inheritedLock)); !adopted) {
                return std::unexpected(adopted.error());
            }
            return listen();
        }

        if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
            return std::unexpected(QStringLiteral("Cannot create overlay socket directory"));
        }
        CUniqueFd   lock{::open(lockPath.toLocal8Bit().constData(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600)};
        struct stat info{};
        if (!lock || ::fstat(lock.get(), &info) || !S_ISREG(info.st_mode) || info.st_uid != ::getuid() || (info.st_mode & 0077)) {
            return std::unexpected(QStringLiteral("Cannot safely open overlay ownership lock"));
        }
        const bool locked = ::flock(lock.get(), LOCK_EX | LOCK_NB) == 0;
        // A pre-feature owner uses QLockFile, not flock. Never unlink its live server.
        const bool live = locked && liveServer(m_path);
        if (!locked || live || command == Command::Restart) {
            lock.reset(); // Closing releases our flock before talking to the resident.
            if (command == Command::Start) {
                return std::unexpected(QStringLiteral("An overlay is already running for this session; use 'toggle'"));
            }
            return forward(command, locked && !live);
        }
        m_lockFd = std::move(lock);
        return listen();
    }

    std::expected<void, QString> OverlayInstance::adoptLock(const QString& lockPath, CUniqueFd inheritedLock) {
        struct stat fdInfo{}, pathInfo{};
        if (::fstat(inheritedLock.get(), &fdInfo) || ::lstat(lockPath.toLocal8Bit().constData(), &pathInfo) || !S_ISREG(fdInfo.st_mode) || fdInfo.st_uid != ::getuid() ||
            fdInfo.st_dev != pathInfo.st_dev || fdInfo.st_ino != pathInfo.st_ino || ::flock(inheritedLock.get(), LOCK_EX | LOCK_NB)) {
            return std::unexpected(QStringLiteral("Cannot adopt restart ownership lock"));
        }
        m_lockFd = std::move(inheritedLock);
        return {};
    }

    std::expected<OverlayInstance::Ownership, QString> OverlayInstance::forward(Command command, bool noResident) const {
        QLocalSocket         client;
        const QDeadlineTimer connectDeadline(ConnectBudget);
        do {
            client.abort();
            client.connectToServer(m_path);
            if (client.waitForConnected(static_cast<int>(ProbeTimeout.count()))) {
                break;
            }
            QThread::msleep(static_cast<unsigned long>(ConnectRetry.count()));
        } while (!connectDeadline.hasExpired());
        if (client.state() != QLocalSocket::ConnectedState) {
            return std::unexpected(noResident ? QStringLiteral("No overlay is running for this session; use toggle to start it.") :
                                                QStringLiteral("Overlay is starting or unresponsive; try again"));
        }

        client.write(command == Command::Restart ? "restart\n" : "toggle\n");
        client.flush();
        QByteArray           response;
        const QDeadlineTimer replyDeadline(command == Command::Restart ? RestartReplyTimeout : ToggleReplyWait);
        while (!response.contains('\n') && response.size() < MaxReplyBytes && !replyDeadline.hasExpired()) {
            if (!client.bytesAvailable() && !client.waitForReadyRead(static_cast<int>(replyDeadline.remainingTime()))) {
                break;
            }
            response += client.readAll();
        }

        if (response == "ok\n") {
            return Ownership::Forwarded;
        }
        if (command == Command::Restart && response == "error: Invalid overlay command\n") {
            return std::unexpected(QStringLiteral("Resident does not support restart; manually close/relaunch the overlay once after upgrading"));
        }
        if (response.startsWith("error: ") && response.endsWith('\n') && response.size() <= MaxReplyBytes) {
            return std::unexpected(QString::fromUtf8(response).trimmed());
        }
        return std::unexpected(QStringLiteral("Overlay did not confirm completion; outcome unknown. Do not automatically retry."));
    }

    std::expected<OverlayInstance::Ownership, QString> OverlayInstance::listen() {
        // Also guard against an older binary still starting. Its lock is held until
        // our socket is listening; a live old owner is never treated as stale.
        QLockFile legacy(m_path + QStringLiteral(".lock"));
        legacy.setStaleLockTime(0);
        if (!legacy.tryLock()) {
            return std::unexpected(QStringLiteral("Legacy overlay is starting or running; relaunch it manually once to support restart"));
        }
        struct stat info{};
        if (::lstat(m_path.toLocal8Bit().constData(), &info) == 0) {
            if (!S_ISSOCK(info.st_mode)) {
                return std::unexpected(QStringLiteral("Refusing to remove non-socket entry: %1").arg(m_path));
            }
            if (!QLocalServer::removeServer(m_path)) {
                return std::unexpected(QStringLiteral("Cannot remove stale overlay socket"));
            }
        } else if (errno != ENOENT) {
            return std::unexpected(QStringLiteral("Cannot inspect overlay socket"));
        }
        if (!m_server.listen(m_path)) {
            return std::unexpected(m_server.errorString());
        }
        return Ownership::Owner;
    }

    QByteArray OverlayInstance::replyLine(const QString& error) {
        if (error.isEmpty()) {
            return QByteArrayLiteral("ok\n");
        }
        QString clean = error;
        clean.replace('\n', ' ');
        clean.replace('\r', ' ');
        return QByteArray("error: ") + clean.toUtf8().left(1000) + '\n';
    }

    void OverlayInstance::reply(QLocalSocket* client, const QString& error) {
        if (!client) {
            return;
        }
        client->write(replyLine(error));
        client->disconnectFromServer();
    }
}
