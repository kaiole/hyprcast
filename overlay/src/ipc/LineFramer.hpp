#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <expected>

namespace Hyprcast::Overlay {
    // Splits a byte stream into newline-terminated frames (CRLF tolerated).
    class LineFramer {
      public:
        static constexpr qsizetype MaxFrameBytes = 4 * 1024 * 1024;

        // Returns the frames completed by `bytes`. On an oversized frame the buffer is
        // discarded and an error returned.
        [[nodiscard]] std::expected<QList<QByteArray>, QString> append(const QByteArray& bytes);
        void                                                    reset() noexcept;

      private:
        QByteArray m_partialLine;
    };
} // namespace Hyprcast::Overlay
