#include "LineFramer.hpp"

namespace Hyprcast::Overlay {
    bool LineFramer::append(const QByteArray& bytes, QList<QByteArray>* completeLines, QString* error) {
        if (error) {
            error->clear();
        }
        if (!completeLines) {
            if (error) {
                *error = QStringLiteral("internal error: line output is null");
            }
            return false;
        }

        completeLines->clear();
        qsizetype offset = 0;
        while (offset < bytes.size()) {
            const qsizetype newline     = bytes.indexOf('\n', offset);
            const qsizetype end         = newline < 0 ? bytes.size() : newline;
            const qsizetype segmentSize = end - offset;
            if (segmentSize > MaxFrameBytes - m_partialLine.size()) {
                reset();
                if (error) {
                    *error = QStringLiteral("IPC frame exceeds the %1-byte limit").arg(MaxFrameBytes);
                }
                completeLines->clear();
                return false;
            }

            m_partialLine.append(bytes.constData() + offset, segmentSize);
            if (newline < 0) {
                break;
            }

            if (m_partialLine.endsWith('\r')) {
                m_partialLine.chop(1);
            }
            completeLines->push_back(std::move(m_partialLine));
            m_partialLine = {};
            offset        = newline + 1;
        }

        return true;
    }

    void LineFramer::reset() noexcept {
        m_partialLine.clear();
    }
} // namespace Hyprcast::Overlay
