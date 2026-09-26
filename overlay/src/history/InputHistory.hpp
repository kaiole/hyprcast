#pragma once

#include "../input/KeyboardInterpreter.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariant>

#include <cstddef>
#include <cstdint>
#include <deque>

namespace Hyprcast::Overlay {
    enum class BackspaceMode {
        Delete,
        Symbol,
    };

    struct InputHistoryOptions {
        BackspaceMode backspaceMode             = BackspaceMode::Delete; // Temporary default until behavior settings are configurable.
        qsizetype     maxRetainedUtf16CodeUnits = 4096;                  // Budget for the rendered QString's UTF-16 code units.
    };

    struct HistoryEntry {
        std::uint64_t     id = 0;
        InterpretedAction action;
    };

    class HistoryListModel : public QAbstractListModel {
        Q_OBJECT
        Q_PROPERTY(QString displayText READ displayText NOTIFY displayTextChanged)

      public:
        enum Role {
            EntryIdRole = Qt::UserRole + 1,
            KindRole,
            TextRole,
            KeyRole,
            ModifiersRole,
            LabelRole,
            KeyboardIdRole,
            KeycodeRole,
            EventTimeMsRole,
            RepeatedRole,
            RepeatCountRole,
        };
        Q_ENUM(Role)

        explicit HistoryListModel(QObject* parent = nullptr);

        [[nodiscard]] int                             rowCount(const QModelIndex& parent = {}) const override;
        [[nodiscard]] QVariant                        data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
        [[nodiscard]] QHash<int, QByteArray>          roleNames() const override;
        [[nodiscard]] QString                         displayText() const;
        [[nodiscard]] const std::deque<HistoryEntry>& entries() const noexcept {
            return m_entries;
        }

        void clear();
        void setSnapshot(const std::deque<HistoryEntry>& entries);

      signals:
        void displayTextChanged();

      protected:
        [[nodiscard]] QString    label(const InterpretedAction& action) const;
        std::deque<HistoryEntry> m_entries;
    };

    class InputHistory final : public HistoryListModel {
      public:
        explicit InputHistory(InputHistoryOptions options = {}, QObject* parent = nullptr);

        // Returns true when an action changed history. Plain Backspace is the only
        // action affected by backspaceMode; modified Backspace remains a chord.
        bool                    apply(const InterpretedAction& action);
        [[nodiscard]] qsizetype retainedUtf16CodeUnits() const;

      private:
        [[nodiscard]] bool      isPlainBackspace(const InterpretedAction& action) const;
        void                    eraseLast();
        void                    eraseLastTextGrapheme();
        void                    trimRetention();
        void                    removeTextPrefix(qsizetype length, std::size_t entryCount);
        void                    removeTextSuffix(qsizetype length, std::size_t entryCount);
        [[nodiscard]] qsizetype nextGraphemeBoundary(const QString& text, qsizetype position) const;

        InputHistoryOptions     m_options;
        std::uint64_t           m_nextEntryId = 1;
    };
} // namespace Hyprcast::Overlay
