#pragma once

#include "hyprcast/os/UniqueFd.hpp"

#include <QByteArray>
#include <QLocalServer>
#include <QObject>
#include <QString>

#include <expected>

class QLocalSocket;

namespace Hyprcast::Overlay {
    // One resident overlay per event socket. The lock protects stale-socket cleanup
    // and closes the simultaneous-first-launch race.
    class OverlayInstance final : public QObject {
        Q_OBJECT
      public:
        enum class Command {
            Start,
            Toggle,
            Restart
        };
        enum class Ownership {
            Owner,     // This process is now the resident overlay.
            Forwarded, // The command was delivered to the existing resident.
        };

        explicit OverlayInstance(const QString& eventSocket, QObject* parent = nullptr);

        // Becomes the resident owner, or forwards `command` to the existing one.
        // `inheritedLock` is the ownership lock handed over by a restarting resident.
        [[nodiscard]] std::expected<Ownership, QString> acquire(Command command, CUniqueFd inheritedLock = {});
        [[nodiscard]] int                               lockDescriptor() const noexcept {
            return m_lockFd.get();
        }

        // Single-line wire reply: "ok\n" or "error: <message>\n".
        [[nodiscard]] static QByteArray replyLine(const QString& error);
        static void                     reply(QLocalSocket* client, const QString& error = {});

      signals:
        void toggleRequested(QLocalSocket* client);
        void restartRequested(QLocalSocket* client);

      private:
        [[nodiscard]] std::expected<void, QString>      adoptLock(const QString& lockPath, CUniqueFd inheritedLock);
        [[nodiscard]] std::expected<Ownership, QString> forward(Command command, bool noResident) const;
        [[nodiscard]] std::expected<Ownership, QString> listen();

        QString                                         m_path;
        // Declared before the server so the lock is released only after the server
        // has stopped listening and removed its socket.
        CUniqueFd    m_lockFd;
        QLocalServer m_server;
    };
}
