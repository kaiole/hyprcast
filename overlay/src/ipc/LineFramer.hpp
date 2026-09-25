#pragma once

#include <QByteArray>
#include <QString>

#include <QList>

namespace Hyprcast::Overlay {
    class LineFramer {
      public:
        static constexpr qsizetype MaxFrameBytes = 4 * 1024 * 1024;

        [[nodiscard]] bool         append(const QByteArray& bytes, QList<QByteArray>* completeLines, QString* error);
        void                       reset() noexcept;

      private:
        QByteArray m_partialLine;
    };
} // namespace Hyprcast::Overlay
