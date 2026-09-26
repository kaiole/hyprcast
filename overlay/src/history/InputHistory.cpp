#include "InputHistory.hpp"

#include <QTextBoundaryFinder>

#include <algorithm>

namespace Hyprcast::Overlay {
    InputHistory::InputHistory(InputHistoryOptions options) : m_options(options) {
        m_options.maxRetainedUtf16CodeUnits = std::max<qsizetype>(0, m_options.maxRetainedUtf16CodeUnits);
    }

    bool InputHistory::apply(const InterpretedAction& action) {
        if (isPlainBackspace(action) && m_options.backspaceMode == BackspaceMode::Delete) {
            if (m_entries.empty()) {
                return false;
            }
            eraseLast();
            return true;
        }

        if (action.kind == InterpretedActionKind::Text) {
            if (action.text.isEmpty()) {
                return false;
            }
        } else if (label(action).isEmpty()) {
            return false;
        }

        m_entries.push_back({.id = m_nextEntryId, .action = action});
        ++m_nextEntryId;
        if (m_nextEntryId == 0) {
            m_nextEntryId = 1;
        }
        trimRetention();
        return true;
    }

    QString InputHistory::displayText() const {
        QString result;
        for (const auto& entry : m_entries) {
            const auto& action = entry.action;
            if (action.kind == InterpretedActionKind::Text) {
                result.append(action.text);
                continue;
            }

            const QString entryLabel = label(action);
            if (entryLabel.isEmpty()) {
                continue;
            }
            if (!result.isEmpty() && !result.endsWith(QLatin1Char(' '))) {
                result.append(QLatin1Char(' '));
            }
            result.append(QLatin1Char('['));
            result.append(entryLabel);
            result.append(QStringLiteral("] "));
        }
        return result;
    }

    qsizetype InputHistory::retainedUtf16CodeUnits() const {
        qsizetype size = 0;
        QChar     lastChar{};
        bool      hasOutput = false;
        for (const auto& entry : m_entries) {
            const auto& action = entry.action;
            if (action.kind == InterpretedActionKind::Text) {
                if (action.text.isEmpty()) {
                    continue;
                }
                size += action.text.size();
                lastChar  = action.text.back();
                hasOutput = true;
                continue;
            }

            const QString entryLabel = label(action);
            if (entryLabel.isEmpty()) {
                continue;
            }
            if (hasOutput && lastChar != QLatin1Char(' ')) {
                ++size;
            }
            size += entryLabel.size() + 3; // brackets and the trailing display separator
            lastChar  = QLatin1Char(' ');
            hasOutput = true;
        }
        return size;
    }

    bool InputHistory::isPlainBackspace(const InterpretedAction& action) const {
        return action.kind == InterpretedActionKind::Key && action.key == QStringLiteral("Backspace") && action.modifiers.isEmpty();
    }

    QString InputHistory::label(const InterpretedAction& action) const {
        if (action.kind == InterpretedActionKind::Chord) {
            QStringList parts = action.modifiers;
            if (!action.key.isEmpty()) {
                parts.push_back(action.key);
            }
            if (parts.isEmpty()) {
                return {};
            }
            return parts.join(QLatin1Char('+'));
        }
        if (action.kind == InterpretedActionKind::Key) {
            return action.key;
        }
        return {};
    }

    void InputHistory::eraseLast() {
        if (m_entries.back().action.kind == InterpretedActionKind::Text) {
            eraseLastTextGrapheme();
        } else {
            m_entries.pop_back();
        }
    }

    void InputHistory::eraseLastTextGrapheme() {
        std::size_t firstTextEntry = m_entries.size();
        while (firstTextEntry > 0 && m_entries[firstTextEntry - 1].action.kind == InterpretedActionKind::Text) {
            --firstTextEntry;
        }

        QString run;
        for (std::size_t i = firstTextEntry; i < m_entries.size(); ++i) {
            run.append(m_entries[i].action.text);
        }
        if (run.isEmpty()) {
            return;
        }

        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, run);
        finder.setPosition(run.size());
        qsizetype boundary = finder.toPreviousBoundary();
        if (boundary < 0 || boundary >= run.size()) {
            boundary = 0;
        }
        removeTextSuffix(run.size() - boundary, m_entries.size() - firstTextEntry);
    }

    void InputHistory::trimRetention() {
        if (m_options.maxRetainedUtf16CodeUnits == 0) {
            m_entries.clear();
            return;
        }

        while (retainedUtf16CodeUnits() > m_options.maxRetainedUtf16CodeUnits && !m_entries.empty()) {
            if (m_entries.front().action.kind != InterpretedActionKind::Text) {
                m_entries.pop_front();
                continue;
            }

            std::size_t firstNonText = 0;
            QString     leadingText;
            while (firstNonText < m_entries.size() && m_entries[firstNonText].action.kind == InterpretedActionKind::Text) {
                leadingText.append(m_entries[firstNonText].action.text);
                ++firstNonText;
            }
            if (leadingText.isEmpty()) {
                m_entries.pop_front();
                continue;
            }

            const qsizetype excess = retainedUtf16CodeUnits() - m_options.maxRetainedUtf16CodeUnits;
            const qsizetype target = std::min(excess, leadingText.size());
            const qsizetype cut    = nextGraphemeBoundary(leadingText, target);
            removeTextPrefix(cut, firstNonText);
        }
    }

    void InputHistory::removeTextPrefix(qsizetype length, std::size_t entryCount) {
        while (length > 0 && entryCount > 0 && !m_entries.empty() && m_entries.front().action.kind == InterpretedActionKind::Text) {
            auto&      text    = m_entries.front().action.text;
            const auto removed = std::min(length, text.size());
            text.remove(0, removed);
            length -= removed;
            if (text.isEmpty()) {
                m_entries.pop_front();
                --entryCount;
            } else {
                break;
            }
        }
    }

    void InputHistory::removeTextSuffix(qsizetype length, std::size_t entryCount) {
        while (length > 0 && entryCount > 0 && !m_entries.empty()) {
            const std::size_t index = m_entries.size() - 1;
            if (m_entries[index].action.kind != InterpretedActionKind::Text) {
                break;
            }
            auto&      text    = m_entries[index].action.text;
            const auto removed = std::min(length, text.size());
            text.chop(removed);
            length -= removed;
            if (text.isEmpty()) {
                m_entries.pop_back();
                --entryCount;
            } else {
                break;
            }
        }
    }

    qsizetype InputHistory::nextGraphemeBoundary(const QString& text, qsizetype position) const {
        if (position <= 0) {
            return 0;
        }
        if (position >= text.size()) {
            return text.size();
        }

        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
        finder.setPosition(position);
        if (finder.isAtBoundary()) {
            return position;
        }
        const qsizetype boundary = finder.toNextBoundary();
        return boundary < 0 ? text.size() : boundary;
    }
} // namespace Hyprcast::Overlay
