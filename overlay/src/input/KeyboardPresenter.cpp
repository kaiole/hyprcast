#include "KeyboardPresenter.hpp"

#include <QDebug>

#include <algorithm>
#include <limits>

namespace Hyprcast::Overlay {
    KeyboardPresenter::KeyboardPresenter(QObject* parent) : QObject(parent) {
        m_timer.setSingleShot(true);
        m_timer.setTimerType(Qt::PreciseTimer);
        connect(&m_timer, &QTimer::timeout, this, [this] { onTimer(); });
        connect(&m_history, &HistoryListModel::displayTextChanged, this, &KeyboardPresenter::outputTextChanged);
    }

    void KeyboardPresenter::processMessage(const ProtocolMessage& message, Clock::time_point now) {
        const QStringList previousHeldKeys = m_interpreter.heldKeys();
        QString           error;
        const auto        actions = m_interpreter.process(message, now, &error);
        if (!error.isEmpty()) {
            qWarning().noquote() << QStringLiteral("hyprcast-overlay: keyboard interpretation failed: %1").arg(error);
        }
        for (const auto& action : actions) {
            append(action, now);
        }
        if (previousHeldKeys != m_interpreter.heldKeys()) {
            emit heldKeysChanged();
        }
        scheduleTimer(now);
    }

    void KeyboardPresenter::advance(Clock::time_point now) {
        const QStringList previousHeldKeys = m_interpreter.heldKeys();
        const auto        actions          = m_interpreter.advance(now);
        for (const auto& action : actions) {
            append(action, now);
        }
        if (previousHeldKeys != m_interpreter.heldKeys()) {
            emit heldKeysChanged();
        }

        if (m_expirationDeadline && now >= *m_expirationDeadline) {
            m_expirationDeadline.reset();
            beginExpiration(now);
        }
        if (m_fadeDeadline && now >= *m_fadeDeadline) {
            finishFade();
        }
        scheduleTimer(now);
    }

    void KeyboardPresenter::resetConnection() {
        const QStringList previousHeldKeys = m_interpreter.heldKeys();
        m_interpreter.reset();
        if (previousHeldKeys != m_interpreter.heldKeys()) {
            emit heldKeysChanged();
        }
        scheduleTimer(Clock::now());
    }

    void KeyboardPresenter::setExpiration(int expireAfterMs, int fadeDurationMs, Clock::time_point now) {
        const int  boundedExpiration = std::max(0, expireAfterMs);
        const int  boundedFade       = std::max(0, fadeDurationMs);
        const bool fadeChanged       = boundedFade != m_fadeDurationMs;
        m_expireAfterMs              = boundedExpiration;
        m_fadeDurationMs             = boundedFade;
        if (fadeChanged) {
            emit fadeDurationMsChanged();
        }

        if (m_expireAfterMs == 0) {
            m_expirationDeadline.reset();
            m_fadeDeadline.reset();
            m_fadingHistory.clear();
            setFading(false);
        } else {
            noteHistoryActivity(now);
        }
        scheduleTimer(now);
    }

    void KeyboardPresenter::append(const InterpretedAction& action, Clock::time_point now) {
        if (m_history.apply(action)) {
            noteHistoryActivity(now);
        }
    }

    void KeyboardPresenter::noteHistoryActivity(Clock::time_point now) {
        if (m_fading) {
            m_fadingHistory.clear();
            setFading(false);
        }

        if (m_history.rowCount() == 0 || m_expireAfterMs == 0) {
            m_expirationDeadline.reset();
            m_fadeDeadline.reset();
            return;
        }

        m_expirationDeadline = now + std::chrono::milliseconds(m_expireAfterMs);
        m_fadeDeadline.reset();
    }

    void KeyboardPresenter::beginExpiration(Clock::time_point now) {
        if (m_history.rowCount() == 0) {
            return;
        }

        if (m_fadeDurationMs > 0) {
            // Expired entries leave editable history immediately. The snapshot exists only
            // for QML's visual fade and can never consume Backspace.
            m_fadingHistory.setSnapshot(m_history.entries());
            m_history.clear();
            setFading(true);
            m_fadeDeadline = now + std::chrono::milliseconds(m_fadeDurationMs);
            if (now >= *m_fadeDeadline) {
                finishFade();
            }
            return;
        }

        m_history.clear();
        m_fadingHistory.clear();
        setFading(false);
    }

    void KeyboardPresenter::finishFade() {
        m_fadeDeadline.reset();
        m_fadingHistory.clear();
        setFading(false);
    }

    void KeyboardPresenter::setFading(bool fading) {
        if (m_fading == fading) {
            return;
        }
        m_fading = fading;
        emit fadingChanged();
    }

    void KeyboardPresenter::scheduleTimer(Clock::time_point now) {
        auto deadline = m_interpreter.nextRepeatDeadline();
        if (m_expirationDeadline && *m_expirationDeadline < deadline) {
            deadline = *m_expirationDeadline;
        }
        if (m_fadeDeadline && *m_fadeDeadline < deadline) {
            deadline = *m_fadeDeadline;
        }
        if (deadline == Clock::time_point::max()) {
            m_timer.stop();
            return;
        }

        const auto remaining = deadline - now;
        const auto delayMs   = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
        const auto bounded   = std::clamp<std::int64_t>(delayMs, 0, std::numeric_limits<int>::max());
        m_timer.start(static_cast<int>(bounded));
    }

    void KeyboardPresenter::onTimer() {
        advance(Clock::now());
    }
} // namespace Hyprcast::Overlay
