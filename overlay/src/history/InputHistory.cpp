#include "InputHistory.hpp"

#include <QTextBoundaryFinder>

#include <algorithm>

namespace Hyprcast::Overlay {
    HistoryListModel::HistoryListModel(QObject* parent) : QAbstractListModel(parent) {}

    int HistoryListModel::rowCount(const QModelIndex& parent) const {
        return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
    }

    QVariant HistoryListModel::data(const QModelIndex& index, int role) const {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
            return {};
        }

        const auto& entry  = m_entries[static_cast<std::size_t>(index.row())];
        const auto& action = entry.action;
        switch (role) {
            case Qt::DisplayRole:
            case LabelRole: return label(action);
            case EntryIdRole: return QVariant::fromValue<qulonglong>(entry.id);
            case KindRole:
                switch (action.kind) {
                    case InterpretedActionKind::Text: return QStringLiteral("text");
                    case InterpretedActionKind::Key: return QStringLiteral("key");
                    case InterpretedActionKind::Chord: return QStringLiteral("chord");
                }
                return {};
            case TextRole: return action.text;
            case KeyRole: return action.key;
            case ModifiersRole: return action.modifiers;
            case KeyboardIdRole: return action.keyboardId;
            case KeycodeRole: return action.keycode;
            case EventTimeMsRole: return action.eventTimeMs;
            case RepeatedRole: return action.repeated;
            case RepeatCountRole: return action.repeatCount;
            default: return {};
        }
    }

    QHash<int, QByteArray> HistoryListModel::roleNames() const {
        return {{EntryIdRole, "entryId"},
                {KindRole, "kind"},
                {TextRole, "text"},
                {KeyRole, "key"},
                {ModifiersRole, "modifiers"},
                {LabelRole, "label"},
                {KeyboardIdRole, "keyboardId"},
                {KeycodeRole, "keycode"},
                {EventTimeMsRole, "eventTimeMs"},
                {RepeatedRole, "repeated"},
                {RepeatCountRole, "repeatCount"}};
    }

    QString HistoryListModel::displayText() const {
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

    void HistoryListModel::clear() {
        if (m_entries.empty()) {
            return;
        }

        const int lastRow = rowCount() - 1;
        beginRemoveRows({}, 0, lastRow);
        m_entries.clear();
        endRemoveRows();
        emit displayTextChanged();
    }

    QString HistoryListModel::label(const InterpretedAction& action) const {
        if (action.kind == InterpretedActionKind::Text) {
            return action.text;
        }
        if (action.kind == InterpretedActionKind::Chord) {
            QStringList parts = action.modifiers;
            if (!action.key.isEmpty()) {
                parts.push_back(action.key);
            }
            return parts.join(QLatin1Char('+'));
        }
        if (action.kind == InterpretedActionKind::Key) {
            return action.key;
        }
        return {};
    }

    void HistoryListModel::setSnapshot(const std::deque<HistoryEntry>& entries) {
        if (m_entries.empty() && entries.empty()) {
            return;
        }

        beginResetModel();
        m_entries = entries;
        endResetModel();
        emit displayTextChanged();
    }

    InputHistory::InputHistory(InputHistoryOptions options, QObject* parent) : HistoryListModel(parent), m_options(options) {
        m_options.maxRetainedUtf16CodeUnits = std::max<qsizetype>(0, m_options.maxRetainedUtf16CodeUnits);
    }

    bool InputHistory::apply(const InterpretedAction& action) {
        if (isPlainBackspace(action) && m_options.backspaceMode == BackspaceMode::Delete) {
            if (m_entries.empty()) {
                return false;
            }
            eraseLast();
            emit displayTextChanged();
            return true;
        }

        if (action.kind == InterpretedActionKind::Text) {
            if (action.text.isEmpty()) {
                return false;
            }
        } else if (label(action).isEmpty()) {
            return false;
        }

        const int row = rowCount();
        beginInsertRows({}, row, row);
        m_entries.push_back({.id = m_nextEntryId, .action = action});
        endInsertRows();

        ++m_nextEntryId;
        if (m_nextEntryId == 0) {
            m_nextEntryId = 1;
        }
        trimRetention();
        emit displayTextChanged();
        return true;
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

    void InputHistory::eraseLast() {
        if (m_entries.back().action.kind == InterpretedActionKind::Text) {
            eraseLastTextGrapheme();
            return;
        }

        const int lastRow = rowCount() - 1;
        beginRemoveRows({}, lastRow, lastRow);
        m_entries.pop_back();
        endRemoveRows();
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
            clear();
            return;
        }

        while (retainedUtf16CodeUnits() > m_options.maxRetainedUtf16CodeUnits && !m_entries.empty()) {
            if (m_entries.front().action.kind != InterpretedActionKind::Text) {
                beginRemoveRows({}, 0, 0);
                m_entries.pop_front();
                endRemoveRows();
                continue;
            }

            std::size_t firstNonText = 0;
            QString     leadingText;
            while (firstNonText < m_entries.size() && m_entries[firstNonText].action.kind == InterpretedActionKind::Text) {
                leadingText.append(m_entries[firstNonText].action.text);
                ++firstNonText;
            }
            if (leadingText.isEmpty()) {
                beginRemoveRows({}, 0, 0);
                m_entries.pop_front();
                endRemoveRows();
                continue;
            }

            const qsizetype excess = retainedUtf16CodeUnits() - m_options.maxRetainedUtf16CodeUnits;
            const qsizetype target = std::min(excess, leadingText.size());
            const qsizetype cut    = nextGraphemeBoundary(leadingText, target);
            removeTextPrefix(cut, firstNonText);
        }
    }

    void InputHistory::removeTextPrefix(qsizetype length, std::size_t entryCount) {
        std::size_t removeCount = 0;
        qsizetype   remaining   = length;
        qsizetype   partialCut  = 0;
        while (remaining > 0 && removeCount < entryCount) {
            const qsizetype textSize = m_entries[removeCount].action.text.size();
            const qsizetype take     = std::min(remaining, textSize);
            remaining -= take;
            if (take == textSize) {
                ++removeCount;
            } else {
                partialCut = take;
                break;
            }
        }

        if (removeCount > 0) {
            beginRemoveRows({}, 0, static_cast<int>(removeCount - 1));
            for (std::size_t i = 0; i < removeCount; ++i) {
                m_entries.pop_front();
            }
            endRemoveRows();
        }

        if (partialCut > 0 && !m_entries.empty()) {
            m_entries.front().action.text.remove(0, partialCut);
            emit dataChanged(index(0), index(0), {Qt::DisplayRole, TextRole, LabelRole});
        }
    }

    void InputHistory::removeTextSuffix(qsizetype length, std::size_t entryCount) {
        std::size_t removeCount = 0;
        qsizetype   remaining   = length;
        qsizetype   partialCut  = 0;
        while (remaining > 0 && removeCount < entryCount) {
            const qsizetype row      = static_cast<qsizetype>(m_entries.size() - 1 - removeCount);
            const qsizetype textSize = m_entries[static_cast<std::size_t>(row)].action.text.size();
            const qsizetype take     = std::min(remaining, textSize);
            remaining -= take;
            if (take == textSize) {
                ++removeCount;
            } else {
                partialCut = take;
                break;
            }
        }

        if (removeCount > 0) {
            const int firstRow = rowCount() - static_cast<int>(removeCount);
            const int lastRow  = rowCount() - 1;
            beginRemoveRows({}, firstRow, lastRow);
            for (std::size_t i = 0; i < removeCount; ++i) {
                m_entries.pop_back();
            }
            endRemoveRows();
        }

        if (partialCut > 0 && !m_entries.empty()) {
            m_entries.back().action.text.chop(partialCut);
            const int lastRow = rowCount() - 1;
            emit      dataChanged(index(lastRow), index(lastRow), {Qt::DisplayRole, TextRole, LabelRole});
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
