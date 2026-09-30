#pragma once

#include "IpcClient.hpp"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <optional>

class QLocalSocket;

namespace Hyprcast::Overlay {
    class CaptureController final : public QObject {
        Q_OBJECT
      public:
        explicit CaptureController(IpcClient& ipc, QObject* parent = nullptr);
        void enableWhenReady();
        void toggle(QLocalSocket* requester);
        void restoreWhenReady(bool enabled, qint64 deadline);
        bool busy() const { return m_waiting || m_target.has_value() || m_blocked; }
        void setBlocked(bool blocked) { m_blocked = blocked; }
        std::optional<bool> confirmedTarget() const {
            return m_ipc.connected() && m_ipc.hasCastingState() ? std::optional<bool>(!m_ipc.paused()) : std::nullopt;
        }

      signals:
        void activeChanged(bool active);
        void commandFailed(const QString& error);
        void restorationFinished(const QString& error);

      private:
        void                   request(bool enabled);
        void                   finish(const QString& error = {});
        IpcClient&             m_ipc;
        QTimer                 m_timeout;
        QPointer<QLocalSocket> m_requester;
        std::optional<bool>    m_target;
        bool                   m_waiting = false;
        bool                   m_waitingTarget = true;
        bool                   m_restoring = false;
        qint64                 m_restoreDeadline = 0;
        bool                   m_blocked = false;
    };
}
