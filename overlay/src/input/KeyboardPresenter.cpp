#include "KeyboardPresenter.hpp"

#include <QDebug>

#include <algorithm>
#include <limits>

namespace Hyprcast::Overlay {
    KeyboardPresenter::KeyboardPresenter(QObject* parent) : QObject(parent) {
        m_repeatTimer.setSingleShot(true);
        m_repeatTimer.setTimerType(Qt::PreciseTimer);
        connect(&m_repeatTimer, &QTimer::timeout, this, [this] { onRepeatTimer(); });
    }

    void KeyboardPresenter::processMessage(const ProtocolMessage& message) {
        QString    error;
        const auto actions = m_interpreter.process(message, KeyboardInterpreter::Clock::now(), &error);
        if (!error.isEmpty()) {
            qWarning().noquote() << QStringLiteral("hyprcast-overlay: keyboard interpretation failed: %1").arg(error);
        }
        for (const auto& action : actions) {
            append(action);
        }
        scheduleRepeatTimer();
    }

    void KeyboardPresenter::resetConnection() {
        m_repeatTimer.stop();
        m_interpreter.reset();
    }

    void KeyboardPresenter::append(const InterpretedAction& action) {
        if (m_history.apply(action)) {
            emit outputTextChanged();
        }
    }

    void KeyboardPresenter::scheduleRepeatTimer() {
        m_repeatTimer.stop();
        const auto deadline = m_interpreter.nextRepeatDeadline();
        if (deadline == KeyboardInterpreter::Clock::time_point::max()) {
            return;
        }

        const auto remaining = deadline - KeyboardInterpreter::Clock::now();
        const auto delayMs   = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
        const auto bounded   = std::clamp<std::int64_t>(delayMs, 0, std::numeric_limits<int>::max());
        m_repeatTimer.start(static_cast<int>(bounded));
    }

    void KeyboardPresenter::onRepeatTimer() {
        const auto actions = m_interpreter.advance(KeyboardInterpreter::Clock::now());
        for (const auto& action : actions) {
            append(action);
        }
        scheduleRepeatTimer();
    }
} // namespace Hyprcast::Overlay
