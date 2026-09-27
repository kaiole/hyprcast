#include "OverlayInstance.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>

#include <cerrno>
#include <sys/stat.h>

namespace Hyprcast::Overlay {
    OverlayInstance::OverlayInstance(const QString& eventSocket, QObject* parent) :
        QObject(parent), m_path(QFileInfo(eventSocket).absoluteFilePath() + QStringLiteral(".overlay")), m_lock(m_path + QStringLiteral(".lock")) {
        m_lock.setStaleLockTime(0);
        m_server.setSocketOptions(QLocalServer::UserAccessOption);
        m_server.setMaxPendingConnections(16);
        connect(&m_server, &QLocalServer::newConnection, this, [this] {
            while (auto* client = m_server.nextPendingConnection()) {
                client->setReadBufferSize(32);
                connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                QTimer::singleShot(5000, client, [client] {
                    client->abort();
                    client->deleteLater();
                });
                connect(client, &QLocalSocket::readyRead, this, [this, client] {
                    if (client->property("handled").toBool()) {
                        return;
                    }
                    if (!client->canReadLine() && client->bytesAvailable() < 32) {
                        return;
                    }
                    client->setProperty("handled", true);
                    if (client->readAll() != QByteArrayLiteral("toggle\n")) {
                        reply(client, QStringLiteral("Invalid overlay command"));
                        return;
                    }
                    emit toggleRequested(client);
                });
            }
        });
    }

    OverlayInstance::Result OverlayInstance::acquire(bool toggle, QString* error) {
        if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
            *error = QStringLiteral("Cannot create overlay socket directory");
            return Result::Error;
        }
        if (!m_lock.tryLock()) {
            if (m_lock.error() != QLockFile::LockFailedError) {
                *error = QStringLiteral("Cannot lock overlay instance");
                return Result::Error;
            }
            if (!toggle) {
                *error = QStringLiteral("An overlay is already running for this session; use 'toggle'");
                return Result::Error;
            }
            QLocalSocket  client;
            QElapsedTimer deadline;
            deadline.start();
            do {
                client.abort();
                client.connectToServer(m_path);
                if (client.waitForConnected(100)) {
                    break;
                }
                QThread::msleep(25);
            } while (deadline.elapsed() < 3000);
            if (client.state() != QLocalSocket::ConnectedState) {
                *error = QStringLiteral("Overlay is starting or unresponsive; try again");
                return Result::Error;
            }
            client.write("toggle\n");
            client.flush();
            QByteArray response;
            deadline.restart();
            while (!response.contains('\n') && response.size() < 1024 && deadline.elapsed() < 4000) {
                if (!client.bytesAvailable() && !client.waitForReadyRead(4000 - int(deadline.elapsed()))) {
                    break;
                }
                response += client.readAll();
            }
            if (response == "ok\n") {
                return Result::Forwarded;
            }
            *error = response.isEmpty() ? QStringLiteral("Overlay did not confirm the toggle") : QString::fromUtf8(response).trimmed();
            return Result::Error;
        }
        struct stat info{};
        const auto  path = m_path.toLocal8Bit();
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
        if (!m_server.listen(m_path)) {
            *error = m_server.errorString();
            return Result::Error;
        }
        return Result::Owner;
    }

    void OverlayInstance::reply(QLocalSocket* client, const QString& error) {
        if (!client) {
            return;
        }
        client->write(error.isEmpty() ? QByteArrayLiteral("ok\n") : QStringLiteral("error: %1\n").arg(error).toUtf8());
        client->disconnectFromServer();
    }
}
