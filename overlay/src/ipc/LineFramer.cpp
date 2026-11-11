#include "LineFramer.hpp"

#include <QList>
#include <QString>
#include <expected>
#include <utility>

namespace hyprcast::overlay {

std::expected<QList<QByteArray>, QString>
LineFramer::append(const QByteArray& bytes) {
    QList<QByteArray> completeLines;
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const qsizetype newline = bytes.indexOf('\n', offset);
        const qsizetype end = newline < 0 ? bytes.size() : newline;
        const qsizetype segmentSize = end - offset;
        if (segmentSize > MaxFrameBytes - m_partialLine.size()) {
            reset();
            return std::unexpected(
                QStringLiteral("IPC frame exceeds the %1-byte limit")
                    .arg(MaxFrameBytes));
        }

        m_partialLine.append(bytes.constData() + offset, segmentSize);
        if (newline < 0) {
            break;
        }

        if (m_partialLine.endsWith('\r')) {
            m_partialLine.chop(1);
        }
        completeLines.push_back(std::exchange(m_partialLine, {}));
        offset = newline + 1;
    }
    return completeLines;
}

void LineFramer::reset() noexcept {
    m_partialLine.clear();
}

} // namespace hyprcast::overlay
