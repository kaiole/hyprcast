#pragma once

#include "IpcClient.hpp"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <optional>

class QLocalSocket;

namespace hyprcast::overlay {

class CaptureController final : public QObject {
    Q_OBJECT
public:
    explicit CaptureController(IpcClient& ipc, QObject* parent = nullptr);
    void enableWhenReady();
    void toggle(QLocalSocket* requester);
    // A capture change is waiting for the plugin.
    [[nodiscard]] bool busy() const {
        return m_waiting || m_target.has_value();
    }
    // The connected plugin has confirmed that it is capturing.
    [[nodiscard]] bool active() const {
        return m_ipc.connected() && m_ipc.hasCastingState() && !m_ipc.paused();
    }

signals:
    void activeChanged(bool active);
    void commandFailed(const QString& error);

private:
    void request(bool enabled);
    void finish(const QString& error = {});
    IpcClient& m_ipc;
    QTimer m_timeout;
    QPointer<QLocalSocket> m_requester;
    std::optional<bool> m_target;
    // The first enable waits here for the plugin to connect and report.
    bool m_waiting = false;
};

} // namespace hyprcast::overlay
