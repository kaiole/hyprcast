#include "CaptureController.hpp"
#include "OverlayInstance.hpp"

#include <QLocalSocket>

namespace Hyprcast::Overlay {
    CaptureController::CaptureController(IpcClient& ipc, QObject* parent) : QObject(parent), m_ipc(ipc) {
        m_timeout.setSingleShot(true);
        m_timeout.setInterval(3000);
        connect(&m_timeout, &QTimer::timeout, this, [this] {
            finish(QStringLiteral("Plugin did not confirm capture; check that the updated Hyprcast plugin is loaded"));
            // A request may have reached the plugin even if its reply was lost.
            // Disconnect to ensure the server disables capture, rather than hiding it only.
            m_ipc.restartConnection();
        });
        connect(&m_ipc, &IpcClient::connectionReset, this, [this] {
            emit activeChanged(false);
            if (m_target) {
                finish(QStringLiteral("Plugin disconnected before confirming capture"));
            }
        });
        connect(&m_ipc, &IpcClient::protocolMessageReceived, this, [this](const ProtocolMessage& message) {
            const auto* state = std::get_if<CastingStateMessage>(&message);
            if (!state) {
                return;
            }
            emit activeChanged(!state->paused);
            if (m_waiting) {
                m_waiting = false;
                request(true);
            } else if (m_target && *m_target == !state->paused) {
                finish();
            }
        });
    }

    void CaptureController::enableWhenReady() {
        m_waiting = true;
        m_timeout.start();
        if (m_ipc.connected() && m_ipc.hasCastingState()) {
            m_waiting = false;
            request(true);
        }
    }

    void CaptureController::toggle(QLocalSocket* requester) {
        if (m_waiting || m_target) {
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
        m_target = enabled;
        m_timeout.start();
        if (!m_ipc.setCaptureEnabled(enabled)) {
            finish(QStringLiteral("Cannot send capture request to plugin"));
        }
    }

    void CaptureController::finish(const QString& error) {
        m_timeout.stop();
        m_waiting = false;
        m_target.reset();
        OverlayInstance::reply(m_requester.data(), error);
        m_requester.clear();
        if (!error.isEmpty()) {
            emit commandFailed(error);
        }
    }
}
