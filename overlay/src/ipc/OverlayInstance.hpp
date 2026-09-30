#pragma once

#include <QLocalServer>
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
        enum class Command { Start, Toggle, Restart };
        ~OverlayInstance() override;
        Result acquire(bool toggle, QString* error) { return acquire(toggle ? Command::Toggle : Command::Start, error); }
        Result acquire(Command command, QString* error, int inheritedLock = -1);
        int lockDescriptor() const { return m_lockFd; }
        static void reply(QLocalSocket* client, const QString& error = {});

      signals:
        void toggleRequested(QLocalSocket* client);
        void restartRequested(QLocalSocket* client);

      private:
        QString      m_path;
        int          m_lockFd = -1;
        QLocalServer m_server;
    };
}
