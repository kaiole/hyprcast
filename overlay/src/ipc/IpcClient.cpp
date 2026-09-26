#include "IpcClient.hpp"

#include "Protocol.hpp"

#include <QDebug>
#include <QIODevice>
#include <QLocalSocket>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace Hyprcast::Overlay {
    IpcClient::IpcClient(QString socketPath, QObject* parent) : QObject(parent), m_socketPath(std::move(socketPath)) {
        m_socket.setReadBufferSize(2 * LineFramer::MaxFrameBytes);
        m_reconnectTimer.setSingleShot(true);
        m_connectionTimeoutTimer.setSingleShot(true);
        m_stableConnectionTimer.setSingleShot(true);

        connect(&m_socket, &QLocalSocket::connected, this, [this] { handleConnected(); });
        connect(&m_socket, &QLocalSocket::disconnected, this, [this] { handleDisconnected(); });
        connect(&m_socket, &QLocalSocket::readyRead, this, [this] { handleReadyRead(); });
        connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) { handleSocketError(error); });
        connect(&m_reconnectTimer, &QTimer::timeout, this, [this] { connectNow(); });
        connect(&m_connectionTimeoutTimer, &QTimer::timeout, this, [this] {
            if (m_socket.state() == QLocalSocket::ConnectedState) {
                return;
            }
            failConnection(QStringLiteral("Connection attempt timed out"));
        });
        connect(&m_stableConnectionTimer, &QTimer::timeout, this, [this] { m_nextReconnectDelayMs = InitialReconnectDelayMs; });
    }

    IpcClient::~IpcClient() {
        m_started = false;
        m_reconnectTimer.stop();
        m_connectionTimeoutTimer.stop();
        m_stableConnectionTimer.stop();
        QObject::disconnect(&m_socket, nullptr, this, nullptr);
        m_socket.abort();
    }

    void IpcClient::start() {
        if (m_started) {
            return;
        }
        m_started = true;
        connectNow();
    }

    QString IpcClient::statusText() const {
        if (m_connected) {
            return QStringLiteral("Connected");
        }
        if (m_connecting) {
            return QStringLiteral("Connecting…");
        }
        if (m_reconnectTimer.isActive()) {
            return QStringLiteral("Reconnecting…");
        }
        if (m_started) {
            return QStringLiteral("Waiting for Hyprcast");
        }
        return QStringLiteral("Stopped");
    }

    QString IpcClient::detailsText() const {
        if (!m_connected) {
            return m_lastError.isEmpty() ? QStringLiteral("Waiting for the Hyprcast event socket") : m_lastError;
        }
        if (!m_state.hasCastingState()) {
            return QStringLiteral("Awaiting casting state and keyboard snapshots");
        }

        QStringList keyboardNames;
        keyboardNames.reserve(m_state.keyboards().size());
        for (const auto& keyboard : m_state.keyboards()) {
            keyboardNames.push_back(QStringLiteral("%1 (#%2)").arg(keyboard.name).arg(keyboard.id));
        }
        const QString keyboards = keyboardNames.isEmpty() ? QStringLiteral("no subscribed keyboards") : keyboardNames.join(QStringLiteral(", "));
        return QStringLiteral("Casting %1 · %2").arg(m_state.paused() ? QStringLiteral("paused") : QStringLiteral("active"), keyboards);
    }

    QString IpcClient::lastEventText() const {
        return m_state.lastEventSummary().isEmpty() ? QStringLiteral("No IPC messages received yet") : m_state.lastEventSummary();
    }

    void IpcClient::connectNow() {
        if (!m_started || m_socket.state() != QLocalSocket::UnconnectedState) {
            return;
        }

        m_reconnectTimer.stop();
        m_connecting = true;
        m_connected  = false;
        m_connectionTimeoutTimer.start(ConnectionTimeoutMs);
        emit stateChanged();
        m_socket.connectToServer(m_socketPath, QIODevice::ReadOnly);
    }

    void IpcClient::handleConnected() {
        m_reconnectTimer.stop();
        m_connectionTimeoutTimer.stop();
        m_connecting = false;
        m_connected  = true;
        m_lastError.clear();
        m_stableConnectionTimer.start(StableConnectionMs);
        emit stateChanged();
    }

    void IpcClient::handleDisconnected() {
        m_connectionTimeoutTimer.stop();
        m_stableConnectionTimer.stop();
        m_connecting = false;
        m_connected  = false;
        resetConnectionState();
        emit stateChanged();
        scheduleReconnect();
    }

    void IpcClient::handleSocketError(QLocalSocket::LocalSocketError error) {
        Q_UNUSED(error);
        m_lastError = m_socket.errorString();
        emit stateChanged();

        if (m_socket.state() == QLocalSocket::UnconnectedState) {
            handleDisconnected();
        } else {
            m_socket.abort();
            if (m_socket.state() == QLocalSocket::UnconnectedState && !m_reconnectTimer.isActive()) {
                handleDisconnected();
            }
        }
    }

    void IpcClient::handleReadyRead() {
        while (m_socket.bytesAvailable() > 0) {
            const QByteArray chunk = m_socket.read(std::min(m_socket.bytesAvailable(), ReadChunkBytes));
            if (chunk.isEmpty()) {
                return;
            }

            QList<QByteArray> frames;
            QString           error;
            if (!m_framer.append(chunk, &frames, &error)) {
                failConnection(error);
                return;
            }

            for (const auto& frame : frames) {
                handleFrame(frame);
                if (!m_connected) {
                    return;
                }
            }
        }
    }

    void IpcClient::handleFrame(const QByteArray& frame) {
        ProtocolMessage message;
        QString         error;
        if (!parseMessage(frame, &message, &error)) {
            failConnection(QStringLiteral("Invalid IPC message: %1").arg(error));
            return;
        }
        if (!m_state.apply(message, &error)) {
            failConnection(QStringLiteral("Invalid IPC state transition: %1").arg(error));
            return;
        }
        // The state transition is committed before consumers interpret the event. This is
        // emitted synchronously on the socket's thread so event order is preserved.
        emit protocolMessageReceived(message);
        emit stateChanged();
    }

    void IpcClient::resetConnectionState() {
        m_state.reset();
        m_framer.reset();
        emit connectionReset();
    }

    void IpcClient::scheduleReconnect() {
        if (!m_started || m_reconnectTimer.isActive()) {
            return;
        }

        const int delayMs      = m_nextReconnectDelayMs;
        m_nextReconnectDelayMs = std::min(m_nextReconnectDelayMs * 2, MaxReconnectDelayMs);
        m_reconnectTimer.start(delayMs);
        emit stateChanged();
    }

    void IpcClient::failConnection(const QString& reason) {
        m_lastError = reason;
        qWarning().noquote() << QStringLiteral("hyprcast-overlay: %1").arg(reason);
        emit protocolError(reason);
        emit stateChanged();

        if (m_socket.state() != QLocalSocket::UnconnectedState) {
            m_socket.abort();
        }
        if (m_socket.state() == QLocalSocket::UnconnectedState && !m_reconnectTimer.isActive()) {
            handleDisconnected();
        }
    }
} // namespace Hyprcast::Overlay
