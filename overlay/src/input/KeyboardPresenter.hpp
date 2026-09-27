#pragma once

#include "KeyboardInterpreter.hpp"
#include "../history/InputHistory.hpp"

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <chrono>
#include <optional>

namespace Hyprcast::Overlay {
    class KeyboardPresenter final : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString outputText READ outputText NOTIFY outputTextChanged)
        Q_PROPERTY(QStringList heldKeys READ heldKeys NOTIFY heldKeysChanged)
        Q_PROPERTY(QVariantList heldKeyItems READ heldKeyItems NOTIFY heldKeysChanged)
        Q_PROPERTY(int heldKeyCount READ heldKeyCount NOTIFY heldKeysChanged)
        Q_PROPERTY(bool fading READ fading NOTIFY fadingChanged)
        Q_PROPERTY(int fadeDurationMs READ fadeDurationMs NOTIFY fadeDurationMsChanged)

      public:
        using Clock = KeyboardInterpreter::Clock;

        explicit KeyboardPresenter(QObject* parent = nullptr);

        [[nodiscard]] QString outputText() const {
            return m_history.displayText();
        }
        [[nodiscard]] QStringList heldKeys() const {
            return m_interpreter.heldKeys();
        }
        [[nodiscard]] QVariantList heldKeyItems() const;
        [[nodiscard]] int          heldKeyCount() const {
            return static_cast<int>(m_interpreter.heldKeys().size());
        }
        [[nodiscard]] bool fading() const noexcept {
            return m_fading;
        }
        [[nodiscard]] int fadeDurationMs() const noexcept {
            return m_fadeDurationMs;
        }
        [[nodiscard]] InputHistory& historyModel() noexcept {
            return m_history;
        }
        [[nodiscard]] HistoryListModel& fadingHistoryModel() noexcept {
            return m_fadingHistory;
        }

        void processMessage(const ProtocolMessage& message, Clock::time_point now = Clock::now());
        void advance(Clock::time_point now = Clock::now());
        void resetConnection();
        void clearHistory();
        void setHistoryOptions(InputHistoryOptions options);
        void setRepeatsEnabled(bool enabled, Clock::time_point now = Clock::now());
        void setExpiration(int expireAfterMs, int fadeDurationMs, Clock::time_point now = Clock::now());

      signals:
        void outputTextChanged();
        void heldKeysChanged();
        void fadingChanged();
        void fadeDurationMsChanged();

      private:
        void                             append(const InterpretedAction& action, Clock::time_point now);
        void                             noteHistoryActivity(Clock::time_point now);
        void                             beginExpiration(Clock::time_point now);
        void                             finishFade();
        void                             setFading(bool fading);
        void                             scheduleTimer(Clock::time_point now);
        void                             onTimer();

        KeyboardInterpreter              m_interpreter;
        QTimer                           m_timer;
        InputHistory                     m_history;
        HistoryListModel                 m_fadingHistory;
        int                              m_expireAfterMs  = 0;
        int                              m_fadeDurationMs = 0;
        bool                             m_fading         = false;
        std::optional<Clock::time_point> m_expirationDeadline;
        std::optional<Clock::time_point> m_fadeDeadline;
    };
} // namespace Hyprcast::Overlay
