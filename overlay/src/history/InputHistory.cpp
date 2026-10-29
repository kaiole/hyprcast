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

        // Canonical label: text as typed, chords as "Mod+Key", keys by name.
        QString rawLabel(const InterpretedAction& action) {
            switch (action.kind) {
                case InterpretedActionKind::Text: return action.text;
                case InterpretedActionKind::Chord: {
                    QStringList parts = action.modifiers;
                    if (!action.key.isEmpty()) {
                        parts.push_back(action.key);
                    }
                    return parts.join(QLatin1Char('+'));
                }
                case InterpretedActionKind::Key: return action.key;
            }
            return {};
        }

        QString kindName(InterpretedActionKind kind) {
            switch (kind) {
                case InterpretedActionKind::Text: return QStringLiteral("text");
                case InterpretedActionKind::Key: return QStringLiteral("key");
                case InterpretedActionKind::Chord: return QStringLiteral("chord");
            }
            return {};
        }

        bool isPlainBackspace(const InterpretedAction& action) {
            return action.kind == InterpretedActionKind::Key && action.key == QStringLiteral("Backspace") && action.modifiers.isEmpty();
        }

        qsizetype nextGraphemeBoundary(const QString& text, qsizetype position) {
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
    } // namespace

    HistoryProjectionModel::HistoryProjectionModel(const HistoryListModel& source, QObject* parent) : QAbstractListModel(parent), m_source(source) {}

    int HistoryProjectionModel::rowCount(const QModelIndex& parent) const {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }

    QString HistoryProjectionModel::mappedText(const QString& text) const {
        QString result = text;
        result.replace(QLatin1Char(' '), m_source.presentationOptions().spaceSymbol);
        return result;
    }

    QString HistoryProjectionModel::mappedKey(const QString& identity) const {
        // Unused modifiers enter history as Key actions on release. Resolve them
        // through the same mapping used by chord modifiers and live held feedback.
        static const QStringList modifierKeys{QStringLiteral("Shift"), QStringLiteral("Ctrl"), QStringLiteral("Alt"), QStringLiteral("Super"), QStringLiteral("AltGr")};
        if (modifierKeys.contains(identity)) {
            return mappedModifier(identity);
        }
        return m_source.presentationOptions().keySymbols.value(identity, identity).toString();
    }

    QString HistoryProjectionModel::mappedModifier(const QString& identity) const {
        return m_source.presentationOptions().modifierSymbols.value(identity, identity).toString();
    }

    QString HistoryProjectionModel::displayLabel(const InterpretedAction& action) const {
        if (action.kind == InterpretedActionKind::Text) {
            return mappedText(action.text);
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
            case KindRole: return kindName(action.kind);
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

    QString HistoryProjectionModel::formatText(bool rich) const {
        QString       result;
        const QString symbolFont = m_source.presentationOptions().symbolFontFamily;
        for (const Row& row : m_rows) {
            const auto& action       = row.source.action;
            const auto  visibleCount = row.counted ? std::min(row.occurrences, static_cast<std::uint32_t>(m_source.presentationOptions().repeatThreshold - 1)) : 1u;
            if (action.kind == InterpretedActionKind::Text) {
                if (row.counted && !result.isEmpty() && !result.endsWith(QLatin1Char(' '))) {
                    result.append(QLatin1Char(' '));
                }
                const QString label = rich ? row.displayLabel.toHtmlEscaped() : row.displayLabel;
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

    bool HistoryProjectionModel::sameRow(const Row& a, const Row& b) {
        const auto& x = a.source.action;
        const auto& y = b.source.action;
        return a.source.id == b.source.id && x.kind == y.kind && x.text == y.text && x.key == y.key && x.modifiers == y.modifiers && a.displayLabel == b.displayLabel &&
            a.displayKey == b.displayKey && a.displayModifiers == b.displayModifiers && a.occurrences == b.occurrences && a.counted == b.counted;
    }

    HistoryProjectionModel::Row HistoryProjectionModel::makeRow(const HistoryEntry& entry, std::uint32_t occurrences, bool counted) const {
        Row row;
        row.source       = entry;
        row.displayLabel = displayLabel(entry.action);
        row.displayKey   = mappedKey(entry.action.key);
        for (const QString& modifier : entry.action.modifiers) {
            row.displayModifiers.push_back(mappedModifier(modifier));
        }
        row.occurrences = occurrences;
        row.counted     = counted;
        return row;
    }

    QSet<quint64> HistoryProjectionModel::refresh(const QSet<quint64>& collapsedRuns) {
        const auto&   entries = m_source.entries();
        const auto&   options = m_source.presentationOptions();
        QSet<quint64> presentRuns;
        for (const auto& entry : entries) {
            if (entry.repeatGroupId != 0) {
                presentRuns.insert(static_cast<quint64>(entry.repeatGroupId));
            }
        }
        // Forget collapsed runs whose entries are gone.
        QSet<quint64> collapsed = collapsedRuns;
        collapsed.intersect(presentRuns);

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
            const std::size_t runLength = end - i;
            const bool        eligibleText =
                !options.countedRepeats || action.kind != InterpretedActionKind::Text || runId == 0 || runLength <= 1 || textRunHasGraphemeBoundaries(entries, i, end);
            const bool reachedThreshold = static_cast<int>(runLength) >= options.repeatThreshold;
            if (options.countedRepeats && runId != 0 && runLength > 1 && eligibleText && (reachedThreshold || collapsed.contains(runId))) {
                if (reachedThreshold) {
                    collapsed.insert(runId);
                }
                nextRows.push_back(makeRow(entries[i], static_cast<std::uint32_t>(runLength), true));
                i = end;
            } else {
                nextRows.push_back(makeRow(entries[i], 1, false));
                ++i;
            }
        }

        applyRows(std::move(nextRows));
        return collapsed;
    }

    // Applies `nextRows` as minimal model changes: keeps the longest run of rows common to
    // both versions (by entry id), removes/inserts around it, and updates changed rows.
    void HistoryProjectionModel::applyRows(std::vector<Row> nextRows) {
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
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            const auto found = newPositions.constFind(static_cast<quint64>(m_rows[i].source.id));
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

        // Decide which kept rows changed before m_rows is reshaped.
        std::vector<std::size_t> changed;
        for (std::size_t i = 0; i < bestLength; ++i) {
            if (!sameRow(m_rows[bestOld + i], nextRows[bestNew + i])) {
                changed.push_back(bestNew + i);
            }
        }

        if (bestOld + bestLength < m_rows.size()) {
            beginRemoveRows({}, static_cast<int>(bestOld + bestLength), rowCount() - 1);
            m_rows.erase(m_rows.begin() + static_cast<std::ptrdiff_t>(bestOld + bestLength), m_rows.end());
            endRemoveRows();
        }
        if (bestOld > 0) {
            beginRemoveRows({}, 0, static_cast<int>(bestOld - 1));
            m_rows.erase(m_rows.begin(), m_rows.begin() + static_cast<std::ptrdiff_t>(bestOld));
            endRemoveRows();
        }
        if (bestNew > 0) {
            beginInsertRows({}, 0, static_cast<int>(bestNew - 1));
            m_rows.insert(m_rows.begin(), std::make_move_iterator(nextRows.begin()), std::make_move_iterator(nextRows.begin() + static_cast<std::ptrdiff_t>(bestNew)));
            endInsertRows();
        }
        const std::size_t suffixStart = bestNew + bestLength;
        if (suffixStart < nextRows.size()) {
            beginInsertRows({}, static_cast<int>(suffixStart), static_cast<int>(nextRows.size() - 1));
            m_rows.insert(m_rows.end(), std::make_move_iterator(nextRows.begin() + static_cast<std::ptrdiff_t>(suffixStart)), std::make_move_iterator(nextRows.end()));
            endInsertRows();
        }
        for (const std::size_t row : changed) {
            m_rows[row] = std::move(nextRows[row]);
            emit dataChanged(index(static_cast<int>(row)), index(static_cast<int>(row)));
        }

        QString text     = formatText(false);
        QString richText = formatText(true);
        if (text != m_displayText || richText != m_displayRichText) {
            m_displayText     = std::move(text);
            m_displayRichText = std::move(richText);
            emit displayTextChanged();
        }
    }

    HistoryListModel::HistoryListModel(QObject* parent) : QAbstractListModel(parent) {
        m_presentation = new HistoryProjectionModel(*this, this); // Owned by this (Qt parent).
        refreshPresentation();
    }

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
            case LabelRole: return rawLabel(action);
            case EntryIdRole: return QVariant::fromValue<qulonglong>(entry.id);
            case KindRole: return kindName(action.kind);
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

            const QString entryLabel = rawLabel(action);
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
        m_collapsedRuns = m_presentation->refresh(m_collapsedRuns);
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
        } else if (rawLabel(action).isEmpty()) {
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
        if (options == m_options) {
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

            const QString entryLabel = rawLabel(action);
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
} // namespace Hyprcast::Overlay
