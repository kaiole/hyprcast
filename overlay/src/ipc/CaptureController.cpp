#include "CaptureController.hpp"
#include "OverlayInstance.hpp"
#include "Restart.hpp"

#include <QLocalSocket>

namespace Hyprcast::Overlay {
    CaptureController::CaptureController(IpcClient& ipc, QObject* parent) : QObject(parent), m_ipc(ipc) {
        m_timeout.setSingleShot(true);
        m_timeout.setInterval(3000);
        connect(&m_timeout, &QTimer::timeout, this, [this] {
            finish(QStringLiteral("Plugin did not confirm capture; check that the updated Hyprcast plugin is loaded"));
            // A request may have reached the plugin even if its reply was lost.
            // Disconnect to ensure the server disables capture, rather than hiding it only.
            if (m_ipc.connected()) m_ipc.restartConnection();
        });
        connect(&m_ipc, &IpcClient::connectionReset, this, [this] {
            emit activeChanged(false);
            if (m_target || m_restoring) {
                finish(QStringLiteral("Plugin disconnected before confirming capture"));
            }
        });
        connect(&m_ipc, &IpcClient::protocolMessageReceived, this, [this](const ProtocolMessage& message) {
            const auto* state = std::get_if<CastingStateMessage>(&message);
            if (!state) {
                return;
            }
            if (m_restoring && monotonicMilliseconds() >= m_restoreDeadline) {
                finish(QStringLiteral("Capture restoration deadline expired"));
                return;
            }
            if (!m_restoring) emit activeChanged(!state->paused);
            if (m_waiting) {
                m_waiting = false;
                if (m_restoring && !m_waitingTarget && state->paused) finish();
                else request(m_waitingTarget);
            } else if (m_target && *m_target == !state->paused) {
                finish();
            }
        });
    }

    void CaptureController::enableWhenReady() {
        m_waitingTarget = true;
        m_waiting = true;
        m_timeout.start(3000);
        if (m_ipc.connected() && m_ipc.hasCastingState()) {
            m_waiting = false;
            request(true);
        }
    }

    void CaptureController::restoreWhenReady(bool enabled, qint64 deadline) {
        m_restoring = true;
        m_restoreDeadline = deadline;
        m_waitingTarget = enabled;
        m_waiting = true;
        if (monotonicMilliseconds() >= deadline) {
            finish(QStringLiteral("Capture restoration deadline expired"));
            return;
        }
        m_timeout.setInterval(qMax(1, int(deadline - monotonicMilliseconds())));
        m_timeout.start();
        if (m_ipc.connected() && m_ipc.hasCastingState()) {
            m_waiting = false;
            if (!enabled && m_ipc.paused()) finish();
            else request(enabled);
        }
    }

    void CaptureController::toggle(QLocalSocket* requester) {
        if (busy()) {
            OverlayInstance::reply(requester, QStringLiteral("Capture change in progress; try again"));
            return;
        }
        if (!m_ipc.connected() || !m_ipc.hasCastingState()) {
            OverlayInstance::reply(requester, QStringLiteral("Hyprcast plugin is unavailable"));
            return;
        }
        m_requester = requester;
        request(m_ipc.paused());
    }

    void CaptureController::request(bool enabled) {
        if (m_restoring && monotonicMilliseconds() >= m_restoreDeadline) {
            finish(QStringLiteral("Capture restoration deadline expired"));
            return;
        }
        m_target = enabled;
        if (m_restoring) m_timeout.start(int(m_restoreDeadline - monotonicMilliseconds()));
        else m_timeout.start(3000);
        if (!m_ipc.setCaptureEnabled(enabled)) {
            finish(QStringLiteral("Cannot send capture request to plugin"));
        }
    }

    void CaptureController::finish(const QString& error) {
        const bool restoring = m_restoring;
        m_restoring = false;
        m_timeout.stop();
        m_waiting = false;
        m_target.reset();
        OverlayInstance::reply(m_requester.data(), error);
        m_requester.clear();
        if (restoring) {
            if (!error.isEmpty() && m_ipc.connected()) m_ipc.restartConnection();
            emit activeChanged(error.isEmpty() && !m_ipc.paused());
            emit restorationFinished(error);
        }
        if (!error.isEmpty()) {
            emit commandFailed(error);
        }
    }
}
