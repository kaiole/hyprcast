#include "KeyboardPresenter.hpp"

#include <QDebug>
#include <QTextBoundaryFinder>

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
        QString addition;
        if (action.kind == InterpretedActionKind::Text) {
            addition = action.text;
        } else if (action.kind == InterpretedActionKind::Chord) {
            QStringList parts = action.modifiers;
            if (!action.key.isEmpty()) {
                parts.push_back(action.key);
            }
            addition = QStringLiteral("[%1]").arg(parts.join(QLatin1Char('+')));
        } else if (!action.key.isEmpty()) {
            addition = QStringLiteral("[%1]").arg(action.key);
        }

        if (addition.isEmpty()) {
            return;
        }
        if (action.kind != InterpretedActionKind::Text && !m_outputText.isEmpty() && !m_outputText.endsWith(QLatin1Char(' '))) {
            m_outputText.append(QLatin1Char(' '));
        }
        m_outputText.append(addition);
        if (action.kind != InterpretedActionKind::Text) {
            m_outputText.append(QLatin1Char(' '));
        }

        if (m_outputText.size() > MaxOutputCharacters) {
            const qsizetype     desiredCut = m_outputText.size() - MaxOutputCharacters;
            QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, m_outputText);
            finder.setPosition(desiredCut);
            qsizetype cut = finder.toNextBoundary();
            if (cut < 0) {
                cut = desiredCut;
            }
            m_outputText.remove(0, cut);
        }
        emit outputTextChanged();
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
