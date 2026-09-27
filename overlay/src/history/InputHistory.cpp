#include "InputHistory.hpp"

#include <QTextBoundaryFinder>

#include <algorithm>

namespace Hyprcast::Overlay {
    namespace {
        // Count semantic neighbors by canonical input and interpretation, not hold session, timestamp, or repeat metadata.
        bool sameCountedInput(const InterpretedAction& a, const InterpretedAction& b) {
            return a.kind == b.kind && a.keyboardId == b.keyboardId && a.keycode == b.keycode && a.text == b.text && a.key == b.key && a.modifiers == b.modifiers;
        }

        bool textRunHasGraphemeBoundaries(const std::deque<HistoryEntry>& entries, std::size_t first, std::size_t last) {
            std::size_t sectionFirst = first;
            while (sectionFirst > 0 && entries[sectionFirst - 1].action.kind == InterpretedActionKind::Text) {
                --sectionFirst;
            }
            std::size_t sectionLast = last;
            while (sectionLast < entries.size() && entries[sectionLast].action.kind == InterpretedActionKind::Text) {
                ++sectionLast;
            }

            QString                section;
            std::vector<qsizetype> offsets;
            offsets.reserve(sectionLast - sectionFirst + 1);
            offsets.push_back(0);
            for (std::size_t i = sectionFirst; i < sectionLast; ++i) {
                section.append(entries[i].action.text);
                offsets.push_back(section.size());
            }

            QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, section);
            for (std::size_t i = first; i < last; ++i) {
                const qsizetype start = offsets[i - sectionFirst];
                const qsizetype end   = offsets[i - sectionFirst + 1];
                if (start == end) {
                    return false;
                }
                finder.setPosition(start);
                if (!finder.isAtBoundary() || finder.toNextBoundary() != end) {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    HistoryProjectionModel::HistoryProjectionModel(const HistoryListModel* source, QObject* parent) : QAbstractListModel(parent), m_source(source) {}

    int HistoryProjectionModel::rowCount(const QModelIndex& parent) const {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }

    QString HistoryProjectionModel::mappedKey(const QString& identity) const {
        // Unused modifiers enter history as Key actions on release. Resolve them
        // through the same mapping used by chord modifiers and live held feedback.
        static const QStringList modifierKeys{QStringLiteral("Shift"), QStringLiteral("Ctrl"), QStringLiteral("Alt"), QStringLiteral("Super"), QStringLiteral("AltGr")};
        if (modifierKeys.contains(identity)) {
            return mappedModifier(identity);
        }
        return m_source->presentationOptions().keySymbols.value(identity, identity).toString();
    }

    QString HistoryProjectionModel::mappedModifier(const QString& identity) const {
        return m_source->presentationOptions().modifierSymbols.value(identity, identity).toString();
    }

    QString HistoryProjectionModel::rawLabel(const InterpretedAction& action) const {
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
        return action.key;
    }

    QString HistoryProjectionModel::displayLabel(const InterpretedAction& action) const {
        if (action.kind == InterpretedActionKind::Text) {
            return action.text;
        }
        if (action.kind == InterpretedActionKind::Chord) {
            QStringList parts;
            for (const QString& modifier : action.modifiers) {
                parts.push_back(mappedModifier(modifier));
            }
            if (!action.key.isEmpty()) {
                parts.push_back(mappedKey(action.key));
            }
            return parts.join(QLatin1Char('+'));
        }
        return mappedKey(action.key);
    }

    QVariant HistoryProjectionModel::data(const QModelIndex& index, int role) const {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
            return {};
        }
        const Row&  row    = m_rows[static_cast<std::size_t>(index.row())];
        const auto& action = row.source.action;
        switch (role) {
            case Qt::DisplayRole:
                if (!row.counted) {
                    return row.displayLabel;
                }
                return row.displayLabel + QStringLiteral(" x") + QString::number(row.occurrences);
            case EntryIdRole: return QVariant::fromValue<qulonglong>(row.source.id);
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
            case LabelRole: return rawLabel(action);
            case DisplayLabelRole: return row.displayLabel;
            case DisplayKeyRole: return row.displayKey;
            case DisplayModifiersRole: return row.displayModifiers;
            case CountedRole: return row.counted;
            case RepeatCountRole: return row.occurrences;
            default: return {};
        }
    }

    QHash<int, QByteArray> HistoryProjectionModel::roleNames() const {
        return {{EntryIdRole, "entryId"},
                {KindRole, "kind"},
                {TextRole, "text"},
                {KeyRole, "key"},
                {ModifiersRole, "modifiers"},
                {LabelRole, "label"},
                {DisplayLabelRole, "displayLabel"},
                {DisplayKeyRole, "displayKey"},
                {DisplayModifiersRole, "displayModifiers"},
                {CountedRole, "counted"},
                {RepeatCountRole, "repeatCount"}};
    }

    QString HistoryProjectionModel::displayText() const {
        return formatText(false);
    }

    QString HistoryProjectionModel::displayRichText() const {
        return formatText(true);
    }

    QString HistoryProjectionModel::formatText(bool rich) const {
        QString       result;
        const QString symbolFont = m_source->presentationOptions().symbolFontFamily;
        for (const Row& row : m_rows) {
            const auto& action = row.source.action;
            const auto visibleCount = row.counted ? std::min(row.occurrences, static_cast<std::uint32_t>(m_source->presentationOptions().repeatThreshold - 1)) : 1u;
            if (action.kind == InterpretedActionKind::Text) {
                if (row.counted && !result.isEmpty() && !result.endsWith(QLatin1Char(' '))) {
                    result.append(QLatin1Char(' '));
                }
                const QString label = rich ? action.text.toHtmlEscaped() : action.text;
                for (std::uint32_t i = 0; i < visibleCount; ++i) {
                    result.append(label);
                }
                if (row.counted) {
                    const QString suffix = QStringLiteral("…%1x").arg(row.occurrences);
                    result.append(rich ? QStringLiteral("<sub><small>%1</small></sub>").arg(suffix) : suffix);
                    result.append(QLatin1Char(' '));
                }
                continue;
            }

            if (!result.isEmpty() && !result.endsWith(QLatin1Char(' '))) {
                result.append(QLatin1Char(' '));
            }
            QString label = row.displayLabel;
            if (rich) {
                label = label.toHtmlEscaped();
                if (action.kind != InterpretedActionKind::Text && !symbolFont.isEmpty()) {
                    label = QStringLiteral("<font face=\"%1\">%2</font>").arg(symbolFont.toHtmlEscaped(), label);
                }
            }
            for (std::uint32_t i = 0; i < visibleCount; ++i) {
                if (i != 0) {
                    result.append(QLatin1Char(' '));
                }
                result.append(label);
            }
            if (row.counted) {
                const QString suffix = QStringLiteral("…%1x").arg(row.occurrences);
                result.append(rich ? QStringLiteral("<sub><small>%1</small></sub>").arg(suffix) : suffix);
            }
            result.append(QLatin1Char(' '));
        }
        return result;
    }

    bool HistoryProjectionModel::sameRow(const Row& a, const Row& b) const {
        const auto& x = a.source.action;
        const auto& y = b.source.action;
        return a.source.id == b.source.id && x.kind == y.kind && x.text == y.text && x.key == y.key && x.modifiers == y.modifiers && a.displayLabel == b.displayLabel &&
            a.displayKey == b.displayKey && a.displayModifiers == b.displayModifiers && a.occurrences == b.occurrences && a.counted == b.counted;
    }

    void HistoryProjectionModel::refresh(QSet<quint64>& collapsedRuns) {
        const QString previousText     = displayText();
        const QString previousRichText = displayRichText();
        const auto&   entries          = m_source->entries();
        const auto&   options          = m_source->presentationOptions();
        QSet<quint64> presentRuns;
        for (const auto& entry : entries) {
            if (entry.repeatGroupId != 0) {
                presentRuns.insert(static_cast<quint64>(entry.repeatGroupId));
            }
        }
        for (auto it = collapsedRuns.begin(); it != collapsedRuns.end();) {
            if (!presentRuns.contains(*it)) {
                it = collapsedRuns.erase(it);
            } else {
                ++it;
            }
        }

        std::vector<Row> nextRows;
        nextRows.reserve(entries.size());
        for (std::size_t i = 0; i < entries.size();) {
            const auto& action = entries[i].action;
            std::size_t end    = i + 1;
            const auto  runId  = static_cast<quint64>(entries[i].repeatGroupId);
            if (options.countedRepeats && runId != 0) {
                while (end < entries.size() && entries[end].repeatGroupId == runId && sameCountedInput(action, entries[end].action)) {
                    ++end;
                }
            }
            const bool eligibleText =
                !options.countedRepeats || action.kind != InterpretedActionKind::Text || runId == 0 || end - i <= 1 || textRunHasGraphemeBoundaries(entries, i, end);
            if (options.countedRepeats && runId != 0 && end - i > 1 && eligibleText && (static_cast<int>(end - i) >= options.repeatThreshold || collapsedRuns.contains(runId))) {
                if (static_cast<int>(end - i) >= options.repeatThreshold) {
                    collapsedRuns.insert(runId);
                }
                Row row;
                row.source       = entries[i];
                row.displayLabel = displayLabel(action);
                row.displayKey   = mappedKey(action.key);
                for (const QString& modifier : action.modifiers) {
                    row.displayModifiers.push_back(mappedModifier(modifier));
                }
                row.occurrences = static_cast<std::uint32_t>(end - i);
                row.counted     = true;
                nextRows.push_back(std::move(row));
                i = end;
                continue;
            }

            Row row;
            row.source       = entries[i];
            row.displayLabel = displayLabel(action);
            row.displayKey   = mappedKey(action.key);
            for (const QString& modifier : action.modifiers) {
                row.displayModifiers.push_back(mappedModifier(modifier));
            }
            nextRows.push_back(std::move(row));
            ++i;
        }

        const auto                  oldRows = m_rows;
        QHash<quint64, std::size_t> newPositions;
        newPositions.reserve(static_cast<qsizetype>(nextRows.size()));
        for (std::size_t i = 0; i < nextRows.size(); ++i) {
            newPositions.insert(static_cast<quint64>(nextRows[i].source.id), i);
        }

        std::size_t bestOld       = 0;
        std::size_t bestNew       = 0;
        std::size_t bestLength    = 0;
        std::size_t currentOld    = 0;
        std::size_t currentNew    = 0;
        std::size_t currentLength = 0;
        for (std::size_t i = 0; i < oldRows.size(); ++i) {
            const auto found = newPositions.constFind(static_cast<quint64>(oldRows[i].source.id));
            if (found == newPositions.cend()) {
                currentLength = 0;
                continue;
            }
            const std::size_t newIndex = found.value();
            if (currentLength > 0 && i == currentOld + currentLength && newIndex == currentNew + currentLength) {
                ++currentLength;
            } else {
                currentOld    = i;
                currentNew    = newIndex;
                currentLength = 1;
            }
            if (currentLength > bestLength) {
                bestOld    = currentOld;
                bestNew    = currentNew;
                bestLength = currentLength;
            }
        }

        if (bestOld + bestLength < m_rows.size()) {
            const int first = static_cast<int>(bestOld + bestLength);
            const int last  = rowCount() - 1;
            beginRemoveRows({}, first, last);
            m_rows.erase(m_rows.begin() + first, m_rows.end());
            endRemoveRows();
        }
        if (bestOld > 0) {
            beginRemoveRows({}, 0, static_cast<int>(bestOld - 1));
            m_rows.erase(m_rows.begin(), m_rows.begin() + static_cast<std::ptrdiff_t>(bestOld));
            endRemoveRows();
        }
        if (bestNew > 0) {
            beginInsertRows({}, 0, static_cast<int>(bestNew - 1));
            m_rows.insert(m_rows.begin(), nextRows.begin(), nextRows.begin() + static_cast<std::ptrdiff_t>(bestNew));
            endInsertRows();
        }
        const std::size_t suffixStart = bestNew + bestLength;
        if (suffixStart < nextRows.size()) {
            beginInsertRows({}, static_cast<int>(suffixStart), static_cast<int>(nextRows.size() - 1));
            m_rows.insert(m_rows.end(), nextRows.begin() + static_cast<std::ptrdiff_t>(suffixStart), nextRows.end());
            endInsertRows();
        }
        for (std::size_t i = 0; i < bestLength; ++i) {
            const std::size_t oldIndex = bestOld + i;
            const std::size_t newIndex = bestNew + i;
            if (!sameRow(oldRows[oldIndex], nextRows[newIndex])) {
                m_rows[newIndex] = nextRows[newIndex];
                emit dataChanged(index(static_cast<int>(newIndex)), index(static_cast<int>(newIndex)));
            }
        }
        if (displayText() != previousText || displayRichText() != previousRichText) {
            emit displayTextChanged();
        }
    }

    HistoryListModel::HistoryListModel(QObject* parent) : QAbstractListModel(parent) {
        m_presentation = new HistoryProjectionModel(this, this);
        refreshPresentation();
    }

    HistoryListModel::~HistoryListModel() = default;

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
        m_collapsedRuns.clear();
        refreshPresentation();
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

    void HistoryListModel::setSnapshot(const std::deque<HistoryEntry>& entries, const HistoryPresentationOptions& presentationOptions, const QSet<quint64>& collapsedRuns) {
        m_presentationOptions = presentationOptions;
        m_collapsedRuns       = collapsedRuns;
        if (!m_entries.empty() || !entries.empty()) {
            beginResetModel();
            m_entries = entries;
            endResetModel();
            emit displayTextChanged();
        }
        refreshPresentation();
    }

    void HistoryListModel::setPresentationOptions(const HistoryPresentationOptions& presentationOptions, const QSet<quint64>& collapsedRuns) {
        m_presentationOptions = presentationOptions;
        m_collapsedRuns       = collapsedRuns;
        refreshPresentation();
    }

    void HistoryListModel::refreshPresentation() {
        m_presentation->refresh(m_collapsedRuns);
    }

    InputHistory::InputHistory(InputHistoryOptions options, QObject* parent) : HistoryListModel(parent), m_options(options) {
        m_options.maxRetainedUtf16CodeUnits = std::max<qsizetype>(0, m_options.maxRetainedUtf16CodeUnits);
        m_presentationOptions               = m_options.presentation;
        refreshPresentation();
    }

    bool InputHistory::apply(const InterpretedAction& action) {
        if (isPlainBackspace(action) && m_options.backspaceMode == BackspaceMode::Delete) {
            if (m_entries.empty()) {
                return false;
            }
            const HistoryEntry removed = m_entries.back();
            eraseLast();
            if (m_entries.empty() || m_entries.back().repeatGroupId != removed.repeatGroupId || !sameCountedInput(m_entries.back().action, removed.action)) {
                // Removing an intervening, different input must not retroactively join the next input to an older group.
                m_groupBoundary = true;
            }
            refreshPresentation();
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

        std::uint64_t repeatGroupId = m_nextEntryId;
        if (!m_groupBoundary && !m_entries.empty() && m_entries.back().repeatGroupId != 0 && sameCountedInput(m_entries.back().action, action)) {
            repeatGroupId = m_entries.back().repeatGroupId;
        }
        m_groupBoundary = false;

        const int row = rowCount();
        beginInsertRows({}, row, row);
        m_entries.push_back({.id = m_nextEntryId, .action = action, .repeatGroupId = repeatGroupId});
        endInsertRows();

        ++m_nextEntryId;
        if (m_nextEntryId == 0) {
            m_nextEntryId = 1;
        }
        trimRetention();
        refreshPresentation();
        emit displayTextChanged();
        return true;
    }

    void InputHistory::setOptions(InputHistoryOptions options) {
        options.maxRetainedUtf16CodeUnits = std::max<qsizetype>(0, options.maxRetainedUtf16CodeUnits);
        if (options.backspaceMode == m_options.backspaceMode && options.maxRetainedUtf16CodeUnits == m_options.maxRetainedUtf16CodeUnits &&
            options.presentation == m_options.presentation) {
            return;
        }

        const QString previousText = displayText();
        m_options                  = options;
        m_presentationOptions      = options.presentation;
        trimRetention();
        refreshPresentation();
        if (displayText() != previousText) {
            emit displayTextChanged();
        }
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
            if (!m_entries.empty()) {
                beginRemoveRows({}, 0, rowCount() - 1);
                m_entries.clear();
                endRemoveRows();
            }
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
