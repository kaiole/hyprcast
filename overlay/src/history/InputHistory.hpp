#pragma once

#include "../input/KeyboardInterpreter.hpp"

#include <QString>

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

    class InputHistory final {
      public:
        explicit InputHistory(InputHistoryOptions options = {});

        // Returns true when an action changed history. Plain Backspace is the only
        // action affected by backspaceMode; modified Backspace remains a chord.
        bool                                          apply(const InterpretedAction& action);

        [[nodiscard]] QString                         displayText() const;
        [[nodiscard]] qsizetype                       retainedUtf16CodeUnits() const;
        [[nodiscard]] const std::deque<HistoryEntry>& entries() const noexcept {
            return m_entries;
        }

      private:
        [[nodiscard]] bool       isPlainBackspace(const InterpretedAction& action) const;
        [[nodiscard]] QString    label(const InterpretedAction& action) const;
        void                     eraseLast();
        void                     eraseLastTextGrapheme();
        void                     trimRetention();
        void                     removeTextPrefix(qsizetype length, std::size_t entryCount);
        void                     removeTextSuffix(qsizetype length, std::size_t entryCount);
        [[nodiscard]] qsizetype  nextGraphemeBoundary(const QString& text, qsizetype position) const;

        InputHistoryOptions      m_options;
        std::deque<HistoryEntry> m_entries;
        std::uint64_t            m_nextEntryId = 1;
    };
} // namespace Hyprcast::Overlay
