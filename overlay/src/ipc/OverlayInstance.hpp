#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QString>

class QLocalSocket;

namespace Hyprcast::Overlay {
    // One resident overlay per event socket. The lock protects stale-socket cleanup
    // and closes the simultaneous-first-launch race.
    class OverlayInstance final : public QObject {
        Q_OBJECT
      public:
        enum class Result {
            Owner,
            Forwarded,
            Error
        };
        explicit OverlayInstance(const QString& eventSocket, QObject* parent = nullptr);
        Result      acquire(bool toggle, QString* error);
        static void reply(QLocalSocket* client, const QString& error = {});

      signals:
        void toggleRequested(QLocalSocket* client);

      private:
        QString      m_path;
        QLockFile    m_lock;
        QLocalServer m_server;
    };
}
