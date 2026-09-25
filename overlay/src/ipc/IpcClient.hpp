#pragma once

#include "ConnectionState.hpp"
#include "LineFramer.hpp"

#include <QLocalSocket>
#include <QObject>
#include <QTimer>

#include <QString>

namespace Hyprcast::Overlay {
    class IpcClient final : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
        Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
        Q_PROPERTY(QString detailsText READ detailsText NOTIFY stateChanged)
        Q_PROPERTY(QString lastEventText READ lastEventText NOTIFY stateChanged)

      public:
        explicit IpcClient(QString socketPath, QObject* parent = nullptr);
        ~IpcClient() override;

        void               start();

        [[nodiscard]] bool connected() const noexcept {
            return m_connected;
        }
        [[nodiscard]] bool hasCastingState() const noexcept {
            return m_state.hasCastingState();
        }
        [[nodiscard]] bool paused() const noexcept {
            return m_state.paused();
        }
        [[nodiscard]] int keyboardCount() const noexcept {
            return m_state.keyboards().size();
        }
        [[nodiscard]] QString        statusText() const;
        [[nodiscard]] QString        detailsText() const;
        [[nodiscard]] QString        lastEventText() const;
        [[nodiscard]] const QString& socketPath() const noexcept {
            return m_socketPath;
        }

      signals:
        void stateChanged();
        void protocolError(const QString& reason);

      private:
        void                    connectNow();
        void                    handleConnected();
        void                    handleDisconnected();
        void                    handleSocketError(QLocalSocket::LocalSocketError error);
        void                    handleReadyRead();
        void                    handleFrame(const QByteArray& frame);
        void                    scheduleReconnect();
        void                    failConnection(const QString& reason);

        static constexpr int    InitialReconnectDelayMs = 250;
        static constexpr int    MaxReconnectDelayMs     = 10'000;
        static constexpr int    ConnectionTimeoutMs     = 3'000;
        static constexpr int    StableConnectionMs      = 10'000;
        static constexpr qint64 ReadChunkBytes          = 64 * 1024;

        QString                 m_socketPath;
        QLocalSocket            m_socket;
        QTimer                  m_reconnectTimer;
        QTimer                  m_connectionTimeoutTimer;
        QTimer                  m_stableConnectionTimer;
        LineFramer              m_framer;
        ConnectionState         m_state;
        QString                 m_lastError;
        int                     m_nextReconnectDelayMs = InitialReconnectDelayMs;
        bool                    m_started              = false;
        bool                    m_connecting           = false;
        bool                    m_connected            = false;
    };
} // namespace Hyprcast::Overlay
