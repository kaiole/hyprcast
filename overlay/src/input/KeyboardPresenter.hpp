#pragma once

#include "KeyboardInterpreter.hpp"

#include <QObject>
#include <QTimer>

namespace Hyprcast::Overlay {
    class KeyboardPresenter final : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString outputText READ outputText NOTIFY outputTextChanged)

      public:
        explicit KeyboardPresenter(QObject* parent = nullptr);

        [[nodiscard]] QString outputText() const {
            return m_outputText;
        }

        void processMessage(const ProtocolMessage& message);
        void resetConnection();

      signals:
        void outputTextChanged();

      private:
        void                       append(const InterpretedAction& action);
        void                       scheduleRepeatTimer();
        void                       onRepeatTimer();

        static constexpr qsizetype MaxOutputCharacters = 512;

        KeyboardInterpreter        m_interpreter;
        QTimer                     m_repeatTimer;
        QString                    m_outputText;
    };
} // namespace Hyprcast::Overlay
