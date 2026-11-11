#include "OverlayInstance.hpp"

#include "hyprcast/os/unique_fd.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <cerrno>
#include <chrono>
#include <expected>
#include <utility>

namespace hyprcast::overlay {
namespace {

using namespace std::chrono_literals;

constexpr qsizetype MaxCommandBytes = 32;
constexpr qsizetype MaxReplyBytes = 1024;
constexpr int MaxPending = 16;
constexpr auto ProbeTimeout = 100ms;
constexpr auto ConnectRetry = 25ms;
constexpr auto ConnectBudget = 3s;
constexpr auto ToggleReplyWait = 4s;
// A restart answers once the window is rebuilt, which loads the theme's QML.
constexpr auto RestartReplyWait = 10s;

} // namespace

OverlayInstance::OverlayInstance(const QString& eventSocket, QObject* parent)
    : QObject(parent),
      m_path(QFileInfo(eventSocket).absoluteFilePath()
             + QStringLiteral(".overlay")) {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(MaxPending);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto* client = m_server.nextPendingConnection()) {
            client->setReadBufferSize(MaxCommandBytes);
            connect(client,
                    &QLocalSocket::disconnected,
                    client,
                    &QObject::deleteLater);
            QTimer::singleShot(RestartReplyWait + 1s, client, [client] {
                client->abort();
                client->deleteLater();
            });
            connect(client, &QLocalSocket::readyRead, this, [this, client] {
                if (!client->canReadLine()
                    && client->bytesAvailable() < MaxCommandBytes) {
                    return;
                }
                // One command per connection; ignore anything that follows.
                disconnect(client, &QLocalSocket::readyRead, this, nullptr);
                const QByteArray request = client->readAll();
                if (request == "toggle\n") {
                    emit toggleRequested(client);
                } else if (request == "restart\n") {
                    emit restartRequested(client);
                } else if (request == "quit\n") {
                    emit quitRequested(client);
                } else {
                    reply(client, QStringLiteral("Invalid overlay command"));
                }
            });
        }
    });
}

std::expected<OverlayInstance::Ownership, QString>
OverlayInstance::acquire(Command command) {
    const QString lockPath = m_path + QStringLiteral(".owner-lock");
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        return std::unexpected(
            QStringLiteral("Cannot create overlay socket directory"));
    }
    unique_fd lock {::open(lockPath.toLocal8Bit().constData(),
                           O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW
                               | O_NONBLOCK,
                           0600)};
    struct stat info {};
    if (!lock || ::fstat(lock.get(), &info) || !S_ISREG(info.st_mode)
        || info.st_uid != ::getuid() || (info.st_mode & 0077u)) {
        return std::unexpected(
            QStringLiteral("Cannot safely open overlay ownership lock"));
    }
    // A resident holds this lock for its whole life, so winning it means there
    // is none.
    const bool noResident = ::flock(lock.get(), LOCK_EX | LOCK_NB) == 0;
    if (noResident && command != Command::Restart && command != Command::Quit) {
        m_lockFd = std::move(lock);
        return listen();
    }
    lock.reset();
    if (noResident && command == Command::Quit) {
        return Ownership::NotRunning;
    }
    if (noResident) {
        return std::unexpected(QStringLiteral(
            "No overlay is running for this session; use toggle to start it."));
    }
    if (command == Command::Start) {
        return std::unexpected(QStringLiteral(
            "An overlay is already running for this session; use 'toggle'"));
    }
    return forward(command);
}

std::expected<OverlayInstance::Ownership, QString>
OverlayInstance::forward(Command command) const {
    QLocalSocket client;
    const QDeadlineTimer connectDeadline(ConnectBudget);
    do {
        client.abort();
        client.connectToServer(m_path);
        if (client.waitForConnected(static_cast<int>(ProbeTimeout.count()))) {
            break;
        }
        // A forwarding client exits after one request and has no event loop
        // to wait on; it backs off while the resident creates its socket.
        // unsigned long is the parameter type of QThread::msleep().
        // NOLINTNEXTLINE(bugprone-unsafe-functions,google-runtime-int)
        QThread::msleep(static_cast<unsigned long>(ConnectRetry.count()));
    } while (!connectDeadline.hasExpired());
    if (client.state() != QLocalSocket::ConnectedState) {
        return std::unexpected(
            QStringLiteral("Overlay is starting or unresponsive; try again"));
    }

    switch (command) {
    case Command::Restart: client.write("restart\n"); break;
    case Command::Quit: client.write("quit\n"); break;
    case Command::Start:
    case Command::Toggle: client.write("toggle\n"); break;
    }
    client.flush();
    QByteArray response;
    const QDeadlineTimer replyDeadline(
        command == Command::Restart ? RestartReplyWait : ToggleReplyWait);
    while (!response.contains('\n') && response.size() < MaxReplyBytes
           && !replyDeadline.hasExpired()) {
        if (!client.bytesAvailable()
            && !client.waitForReadyRead(
                static_cast<int>(replyDeadline.remainingTime()))) {
            break;
        }
        response += client.readAll();
    }

    if (response == "ok\n") {
        return Ownership::Forwarded;
    }
    if (response.startsWith("error: ") && response.endsWith('\n')
        && response.size() <= MaxReplyBytes) {
        return std::unexpected(QString::fromUtf8(response).trimmed());
    }
    return std::unexpected(
        QStringLiteral("Overlay did not confirm completion; outcome unknown. "
                       "Do not automatically retry."));
}

std::expected<OverlayInstance::Ownership, QString> OverlayInstance::listen() {
    // We hold the ownership lock, so a socket left at the path is stale.
    struct stat info {};
    if (::lstat(m_path.toLocal8Bit().constData(), &info) == 0) {
        if (!S_ISSOCK(info.st_mode)) {
            return std::unexpected(
                QStringLiteral("Refusing to remove non-socket entry: %1")
                    .arg(m_path));
        }
        if (!QLocalServer::removeServer(m_path)) {
            return std::unexpected(
                QStringLiteral("Cannot remove stale overlay socket"));
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
    // Written now, so the reply survives a resident that quits right after.
    client->flush();
    client->disconnectFromServer();
}

} // namespace hyprcast::overlay
