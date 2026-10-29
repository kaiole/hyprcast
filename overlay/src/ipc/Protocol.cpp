#include "Protocol.hpp"

#include "hyprcast/protocol/Wire.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1StringView>

#include <cmath>
#include <limits>
#include <string_view>

namespace Hyprcast::Overlay {
    namespace {
        using namespace Wire;

        QLatin1StringView latin1(std::string_view text) {
            return QLatin1StringView(text.data(), static_cast<qsizetype>(text.size()));
        }

        // Reads typed fields from one message. The first failure is kept and later reads
        // return defaults, so a whole message can be read in one expression and checked once.
        class FieldReader {
          public:
            explicit FieldReader(const QJsonObject& object) : m_object(object) {}

            [[nodiscard]] bool ok() const noexcept {
                return m_error.isEmpty();
            }
            [[nodiscard]] const QString& error() const noexcept {
                return m_error;
            }

            QString string(std::string_view name) {
                const auto value = m_object.value(latin1(name));
                if (!value.isString()) {
                    fail(QStringLiteral("field '%1' must be a string").arg(latin1(name)));
                    return {};
                }
                return value.toString();
            }

            QString nonEmptyString(std::string_view name) {
                QString value = string(name);
                if (ok() && value.isEmpty()) {
                    fail(QStringLiteral("field '%1' must not be empty").arg(latin1(name)));
                }
                return value;
            }

            bool boolean(std::string_view name) {
                const auto value = m_object.value(latin1(name));
                if (!value.isBool()) {
                    fail(QStringLiteral("field '%1' must be a boolean").arg(latin1(name)));
                    return false;
                }
                return value.toBool();
            }

            std::uint32_t uint32(std::string_view name) {
                const auto value = m_object.value(latin1(name));
                if (!value.isDouble()) {
                    fail(QStringLiteral("field '%1' must be an unsigned 32-bit integer").arg(latin1(name)));
                    return 0;
                }
                const double number = value.toDouble();
                if (!std::isfinite(number) || number < 0.0 || number > std::numeric_limits<std::uint32_t>::max() || std::floor(number) != number) {
                    fail(QStringLiteral("field '%1' is outside the unsigned 32-bit integer range").arg(latin1(name)));
                    return 0;
                }
                return static_cast<std::uint32_t>(number);
            }

            KeyboardId keyboardId() {
                const KeyboardId id = uint32(FIELD_KEYBOARD_ID);
                if (ok() && id == GLOBAL_KEYBOARD_ID) {
                    fail(QStringLiteral("field 'keyboard_id' must be greater than zero for this event"));
                }
                return id;
            }

            SModifiers modifiers() {
                return {.depressed = uint32(FIELD_DEPRESSED), .latched = uint32(FIELD_LATCHED), .locked = uint32(FIELD_LOCKED), .group = uint32(FIELD_GROUP)};
            }

            SRepeatInfo repeatInfo() {
                const SRepeatInfo info{.rate = uint32(FIELD_RATE), .delay = uint32(FIELD_DELAY)};
                constexpr auto    intMax = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
                if (ok() && (info.rate > intMax || info.delay > intMax)) {
                    fail(QStringLiteral("repeat settings exceed the signed integer range"));
                }
                return info;
            }

          private:
            void fail(QString message) {
                if (m_error.isEmpty()) {
                    m_error = std::move(message);
                }
            }

            const QJsonObject& m_object;
            QString            m_error;
        };

        // Braced initializers evaluate left to right, so the first invalid field is reported.
        template <typename Message>
        std::expected<ProtocolMessage, QString> finish(const FieldReader& reader, Message message) {
            if (!reader.ok()) {
                return std::unexpected(reader.error());
            }
            return ProtocolMessage{std::move(message)};
        }
    } // namespace

    std::expected<ProtocolMessage, QString> parseMessage(const QByteArray& jsonLine) {
        QJsonParseError parseError{};
        const auto      document = QJsonDocument::fromJson(jsonLine, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return std::unexpected(QStringLiteral("invalid JSON object: %1").arg(parseError.errorString()));
        }

        const QJsonObject object = document.object();
        FieldReader       read(object);
        const QString     event = read.string(FIELD_EVENT);
        if (!read.ok()) {
            return std::unexpected(read.error());
        }

        if (event == latin1(EVENT_CASTING_STATE)) {
            if (read.uint32(FIELD_KEYBOARD_ID) != GLOBAL_KEYBOARD_ID && read.ok()) {
                return std::unexpected(QStringLiteral("casting_state requires keyboard_id 0"));
            }
            return finish(read, CastingStateMessage{.paused = read.boolean(FIELD_PAUSED)});
        }
        if (event == latin1(EVENT_SUBSCRIBE_KEYBOARD)) {
            return finish(read,
                          KeyboardSnapshotMessage{
                              .id         = read.keyboardId(),
                              .name       = read.string(FIELD_NAME),
                              .modifiers  = read.modifiers(),
                              .keymap     = read.nonEmptyString(FIELD_KEYMAP),
                              .repeatInfo = read.repeatInfo(),
                          });
        }
        if (event == latin1(EVENT_UNSUBSCRIBE_KEYBOARD)) {
            return finish(read, UnsubscribeKeyboardMessage{.id = read.keyboardId()});
        }

        const KeyboardId keyboardId = read.keyboardId();
        if (!read.ok()) {
            return std::unexpected(read.error());
        }
        if (event == latin1(EVENT_KEY)) {
            KeyMessage    key{.keyboardId = keyboardId, .timeMs = read.uint32(FIELD_TIME_MS), .keycode = read.uint32(FIELD_KEYCODE)};
            const QString state = read.string(FIELD_STATE);
            if (read.ok() && state != latin1(STATE_PRESSED) && state != latin1(STATE_RELEASED)) {
                return std::unexpected(QStringLiteral("field 'state' must be 'pressed' or 'released'"));
            }
            key.pressed = state == latin1(STATE_PRESSED);
            return finish(read, key);
        }
        if (event == latin1(EVENT_MODIFIERS)) {
            return finish(read, ModifiersMessage{.keyboardId = keyboardId, .modifiers = read.modifiers()});
        }
        if (event == latin1(EVENT_KEYMAP)) {
            return finish(read, KeymapMessage{.keyboardId = keyboardId, .keymap = read.nonEmptyString(FIELD_KEYMAP)});
        }
        if (event == latin1(EVENT_REPEAT_INFO)) {
            return finish(read, RepeatInfoMessage{.keyboardId = keyboardId, .repeatInfo = read.repeatInfo()});
        }
        return std::unexpected(QStringLiteral("unsupported event '%1'").arg(event));
    }
} // namespace Hyprcast::Overlay
