#include "IpcClient.hpp"

#include "LineFramer.hpp"
#include "Protocol.hpp"
#include "hyprcast/protocol/Wire.hpp"

#include <QDebug>
#include <QIODevice>
#include <QLocalSocket>
#include <QObject>
#include <algorithm>
#include <string_view>
#include <utility>

namespace Hyprcast::Overlay {

IpcClient::IpcClient(QString socketPath, QObject* parent)
    : QObject(parent),
      m_socketPath(std::move(socketPath)) {
    m_socket.setReadBufferSize(2 * LineFramer::MaxFrameBytes);
    m_reconnectTimer.setSingleShot(true);
    m_connectionTimeoutTimer.setSingleShot(true);
    m_stableConnectionTimer.setSingleShot(true);

    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        handleConnected();
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        handleDisconnected();
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, [this] {
        handleReadyRead();
    });
    connect(&m_socket,
            &QLocalSocket::errorOccurred,
            this,
            [this](QLocalSocket::LocalSocketError error) {
                handleSocketError(error);
            });
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] {
        connectNow();
    });
    connect(&m_connectionTimeoutTimer, &QTimer::timeout, this, [this] {
        if (m_socket.state() == QLocalSocket::ConnectedState) {
            return;
        }
        failConnection(QStringLiteral("Connection attempt timed out"));
    });
    connect(&m_stableConnectionTimer, &QTimer::timeout, this, [this] {
        m_nextReconnectDelayMs = InitialReconnectDelayMs;
    });
}

// Not rule-of-zero on purpose: destroying m_socket can emit
// disconnected/errorOccurred, and our handlers would then touch members that
// are already destroyed. Detach first.
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

void IpcClient::restartConnection() {
    failConnection(QStringLiteral("Capture control timed out"));
}

bool IpcClient::setCaptureEnabled(bool enabled) {
    if (!m_connected || !m_state.hasCastingState()) {
        return false;
    }
    const std::string_view name =
        enabled ? Wire::COMMAND_ENABLE : Wire::COMMAND_DISABLE;
    const QByteArray command =
        QByteArray(name.data(), static_cast<qsizetype>(name.size())) + '\n';
    return m_socket.write(command) == command.size();
}

void IpcClient::connectNow() {
    if (!m_started || m_socket.state() != QLocalSocket::UnconnectedState) {
        return;
    }

    m_reconnectTimer.stop();
    m_connected = false;
    m_connectionTimeoutTimer.start(ConnectionTimeoutMs);
    m_socket.connectToServer(m_socketPath, QIODevice::ReadWrite);
}

void IpcClient::handleConnected() {
    m_reconnectTimer.stop();
    m_connectionTimeoutTimer.stop();
    m_connected = true;
    m_stableConnectionTimer.start(StableConnectionMs);
}

void IpcClient::handleDisconnected() {
    m_connectionTimeoutTimer.stop();
    m_stableConnectionTimer.stop();
    m_connected = false;
    resetConnectionState();
    scheduleReconnect();
}

void IpcClient::handleSocketError(QLocalSocket::LocalSocketError error) {
    // A refused or missing socket is the normal state while Hyprcast is not
    // loaded, so socket errors only drive the reconnect loop and are not
    // reported.
    Q_UNUSED(error);
    if (m_socket.state() == QLocalSocket::UnconnectedState) {
        handleDisconnected();
    } else {
        m_socket.abort();
        if (m_socket.state() == QLocalSocket::UnconnectedState
            && !m_reconnectTimer.isActive()) {
            handleDisconnected();
        }
    }
}

void IpcClient::handleReadyRead() {
    while (m_socket.bytesAvailable() > 0) {
        const QByteArray chunk =
            m_socket.read(std::min(m_socket.bytesAvailable(), ReadChunkBytes));
        if (chunk.isEmpty()) {
            return;
        }

        const auto frames = m_framer.append(chunk);
        if (!frames) {
            failConnection(frames.error());
            return;
        }

        for (const auto& frame : *frames) {
            handleFrame(frame);
            if (!m_connected) {
                return;
            }
        }
    }
}

void IpcClient::handleFrame(const QByteArray& frame) {
    const auto message = parseMessage(frame);
    if (!message) {
        failConnection(
            QStringLiteral("Invalid IPC message: %1").arg(message.error()));
        return;
    }
    if (const auto applied = m_state.apply(*message); !applied) {
        failConnection(QStringLiteral("Invalid IPC state transition: %1")
                           .arg(applied.error()));
        return;
    }
    // The state transition is committed before consumers interpret the event.
    // This is emitted synchronously on the socket's thread so event order is
    // preserved.
    emit protocolMessageReceived(*message);
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

    const int delayMs = m_nextReconnectDelayMs;
    m_nextReconnectDelayMs =
        std::min(m_nextReconnectDelayMs * 2, MaxReconnectDelayMs);
    m_reconnectTimer.start(delayMs);
}

void IpcClient::failConnection(const QString& reason) {
    qWarning().noquote() << QStringLiteral("hyprcast-overlay: %1").arg(reason);

    if (m_socket.state() != QLocalSocket::UnconnectedState) {
        m_socket.abort();
    }
    if (m_socket.state() == QLocalSocket::UnconnectedState
        && !m_reconnectTimer.isActive()) {
        handleDisconnected();
    }
}

} // namespace Hyprcast::Overlay
