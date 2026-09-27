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

      signals:
        void activeChanged(bool active);
        void commandFailed(const QString& error);

      private:
        void                   request(bool enabled);
        void                   finish(const QString& error = {});
        IpcClient&             m_ipc;
        QTimer                 m_timeout;
        QPointer<QLocalSocket> m_requester;
        std::optional<bool>    m_target;
        bool                   m_waiting = false;
    };
}
