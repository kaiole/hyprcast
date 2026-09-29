#pragma once

#include "../input/KeyboardInterpreter.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariant>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace Hyprcast::Overlay {
    enum class BackspaceMode {
        Delete,
        Symbol,
    };

    struct HistoryEntry {
        std::uint64_t     id = 0;
        InterpretedAction action;
        std::uint64_t     repeatGroupId = 0; // Adjacent equivalent inputs; independent of auto-repeat hold sessions.
    };

    struct HistoryPresentationOptions {
        bool        countedRepeats  = false;
        int         repeatThreshold = 4;
        QString     symbolFontFamily;
        QString     spaceSymbol = QStringLiteral(" ");
        QVariantMap keySymbols;
        QVariantMap modifierSymbols;
        friend bool operator==(const HistoryPresentationOptions&, const HistoryPresentationOptions&) = default;
    };

    struct InputHistoryOptions {
        BackspaceMode              backspaceMode             = BackspaceMode::Delete;
        qsizetype                  maxRetainedUtf16CodeUnits = 4096; // Stable budget for the expanded canonical formatted history.
        HistoryPresentationOptions presentation;
    };

    class HistoryListModel;

    class HistoryProjectionModel final : public QAbstractListModel {
        Q_OBJECT
        Q_PROPERTY(QString displayText READ displayText NOTIFY displayTextChanged)
        Q_PROPERTY(QString displayRichText READ displayRichText NOTIFY displayTextChanged)
      public:
        enum Role {
            EntryIdRole = Qt::UserRole + 1,
            KindRole,
            TextRole,
            KeyRole,
            ModifiersRole,
            LabelRole,
            DisplayLabelRole,
            DisplayKeyRole,
            DisplayModifiersRole,
            CountedRole,
            RepeatCountRole,
        };
        Q_ENUM(Role)

        explicit HistoryProjectionModel(const HistoryListModel* source, QObject* parent = nullptr);
        [[nodiscard]] int                    rowCount(const QModelIndex& parent = {}) const override;
        [[nodiscard]] QVariant               data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
        [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
        [[nodiscard]] QString                displayText() const;
        [[nodiscard]] QString                displayRichText() const;
        void                                 refresh(QSet<quint64>& collapsedRuns);

      signals:
        void displayTextChanged();

      private:
        struct Row {
            HistoryEntry  source;
            QString       displayLabel;
            QString       displayKey;
            QStringList   displayModifiers;
            std::uint32_t occurrences = 1;
            bool          counted     = false;
        };
        [[nodiscard]] QString   rawLabel(const InterpretedAction& action) const;
        [[nodiscard]] QString   displayLabel(const InterpretedAction& action) const;
        [[nodiscard]] QString   mappedText(const QString& text) const;
        [[nodiscard]] QString   mappedKey(const QString& identity) const;
        [[nodiscard]] QString   mappedModifier(const QString& identity) const;
        [[nodiscard]] bool      sameRow(const Row& a, const Row& b) const;
        [[nodiscard]] QString   formatText(bool rich) const;

        const HistoryListModel* m_source;
        std::vector<Row>        m_rows;
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
        ~HistoryListModel() override;

        [[nodiscard]] HistoryProjectionModel& presentationModel() noexcept {
            return *m_presentation;
        }
        [[nodiscard]] const HistoryPresentationOptions& presentationOptions() const noexcept {
            return m_presentationOptions;
        }
        [[nodiscard]] const QSet<quint64>& collapsedRepeatRuns() const noexcept {
            return m_collapsedRuns;
        }
        [[nodiscard]] int                             rowCount(const QModelIndex& parent = {}) const override;
        [[nodiscard]] QVariant                        data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
        [[nodiscard]] QHash<int, QByteArray>          roleNames() const override;
        [[nodiscard]] QString                         displayText() const;
        [[nodiscard]] const std::deque<HistoryEntry>& entries() const noexcept {
            return m_entries;
        }

        void clear();
        void setSnapshot(const std::deque<HistoryEntry>& entries, const HistoryPresentationOptions& presentationOptions = {}, const QSet<quint64>& collapsedRuns = {});
        void setPresentationOptions(const HistoryPresentationOptions& presentationOptions, const QSet<quint64>& collapsedRuns = {});

      signals:
        void displayTextChanged();

      protected:
        [[nodiscard]] QString      label(const InterpretedAction& action) const;
        void                       refreshPresentation();
        std::deque<HistoryEntry>   m_entries;
        HistoryPresentationOptions m_presentationOptions;
        HistoryProjectionModel*    m_presentation = nullptr;
        QSet<quint64>              m_collapsedRuns;
    };

    class InputHistory final : public HistoryListModel {
      public:
        explicit InputHistory(InputHistoryOptions options = {}, QObject* parent = nullptr);

        // Returns true when an action changed history. Plain Backspace is the only
        // action affected by backspaceMode; modified Backspace remains a chord.
        bool                    apply(const InterpretedAction& action);
        void                    setOptions(InputHistoryOptions options);
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
        std::uint64_t           m_nextEntryId   = 1;
        bool                    m_groupBoundary = false; // Prevents a later append from bridging an erased intervening input.
    };
} // namespace Hyprcast::Overlay
