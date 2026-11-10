#include "Protocol.hpp"

#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/repeat_info.hpp"
#include "hyprcast/protocol/wire.hpp"

#include <QLatin1StringView>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <cstdint>
#include <expected>
#include <limits>
#include <string_view>
#include <utility>

namespace hyprcast::overlay {
namespace {

using namespace wire;

// The alias lives in qstringfwd.h, which is not self-contained, so the check
// wants whichever header happens to include that one first.
// NOLINTNEXTLINE(misc-include-cleaner)
QLatin1StringView latin1(std::string_view text) {
    return QLatin1StringView(text.data(), static_cast<qsizetype>(text.size()));
}

// Reads typed fields from one message. The first failure is kept and later
// reads return defaults, so a whole message can be read in one expression and
// checked once.
class FieldReader {
public:
    explicit FieldReader(const QJsonObject& object) : m_object(&object) {}

    [[nodiscard]] bool ok() const noexcept { return m_error.isEmpty(); }
    [[nodiscard]] const QString& error() const noexcept { return m_error; }

    QString string(std::string_view name) {
        const auto value = m_object->value(latin1(name));
        if (!value.isString()) {
            fail(QStringLiteral("field '%1' must be a string")
                     .arg(latin1(name)));
            return {};
        }
        return value.toString();
    }

    QString nonEmptyString(std::string_view name) {
        QString value = string(name);
        if (ok() && value.isEmpty()) {
            fail(QStringLiteral("field '%1' must not be empty")
                     .arg(latin1(name)));
        }
        return value;
    }

    bool boolean(std::string_view name) {
        const auto value = m_object->value(latin1(name));
        if (!value.isBool()) {
            fail(QStringLiteral("field '%1' must be a boolean")
                     .arg(latin1(name)));
            return false;
        }
        return value.toBool();
    }

    std::uint32_t uint32(std::string_view name) {
        const auto value = m_object->value(latin1(name));
        if (!value.isDouble()) {
            fail(QStringLiteral("field '%1' must be an unsigned 32-bit integer")
                     .arg(latin1(name)));
            return 0;
        }
        // toInteger() yields the fallback for fractions and for magnitudes
        // beyond qint64.
        const qint64 number = value.toInteger(-1);
        if (!std::in_range<std::uint32_t>(number)) {
            fail(QStringLiteral(
                     "field '%1' is outside the unsigned 32-bit integer range")
                     .arg(latin1(name)));
            return 0;
        }
        return static_cast<std::uint32_t>(number);
    }

    keyboard_id keyboardId() {
        const keyboard_id id = uint32(field_keyboard_id);
        if (ok() && id == global_keyboard_id) {
            fail(QStringLiteral("field 'keyboard_id' must be greater than zero "
                                "for this event"));
        }
        return id;
    }

    modifier_state modifiers() {
        return {
            .depressed = uint32(field_depressed),
            .latched = uint32(field_latched),
            .locked = uint32(field_locked),
            .group = uint32(field_group),
        };
    }

    repeat_info repeatInfo() {
        const repeat_info info {
            .rate = uint32(field_rate),
            .delay = uint32(field_delay),
        };
        constexpr auto intMax =
            static_cast<std::uint32_t>(std::numeric_limits<int>::max());
        if (ok() && (info.rate > intMax || info.delay > intMax)) {
            fail(QStringLiteral(
                "repeat settings exceed the signed integer range"));
        }
        return info;
    }

private:
    void fail(QString message) {
        if (m_error.isEmpty()) {
            m_error = std::move(message);
        }
    }

    const QJsonObject* m_object;
    QString m_error;
};

// Braced initializers evaluate left to right, so the first invalid field is
// reported.
template <typename Message>
std::expected<ProtocolMessage, QString> finish(const FieldReader& reader,
                                               Message message) {
    if (!reader.ok()) {
        return std::unexpected(reader.error());
    }
    return ProtocolMessage {std::move(message)};
}

} // namespace

std::expected<ProtocolMessage, QString>
parseMessage(const QByteArray& jsonLine) {
    QJsonParseError parseError {};
    const auto document = QJsonDocument::fromJson(jsonLine, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(QStringLiteral("invalid JSON object: %1")
                                   .arg(parseError.errorString()));
    }

    const QJsonObject object = document.object();
    FieldReader read(object);
    const QString event = read.string(field_event);
    if (!read.ok()) {
        return std::unexpected(read.error());
    }

    if (event == latin1(event_casting_state)) {
        if (read.uint32(field_keyboard_id) != global_keyboard_id && read.ok()) {
            return std::unexpected(
                QStringLiteral("casting_state requires keyboard_id 0"));
        }
        return finish(
            read,
            CastingStateMessage {.paused = read.boolean(field_paused)});
    }
    if (event == latin1(event_subscribe_keyboard)) {
        return finish(read,
                      KeyboardSnapshotMessage {
                          .id = read.keyboardId(),
                          .name = read.string(field_name),
                          .modifiers = read.modifiers(),
                          .keymap = read.nonEmptyString(field_keymap),
                          .repeatInfo = read.repeatInfo(),
                      });
    }
    if (event == latin1(event_unsubscribe_keyboard)) {
        return finish(read,
                      UnsubscribeKeyboardMessage {.id = read.keyboardId()});
    }

    const keyboard_id keyboardId = read.keyboardId();
    if (!read.ok()) {
        return std::unexpected(read.error());
    }
    if (event == latin1(event_key)) {
        KeyMessage key {
            .keyboardId = keyboardId,
            .timeMs = read.uint32(field_time_ms),
            .keycode = read.uint32(field_keycode),
        };
        const QString state = read.string(field_state);
        if (read.ok() && state != latin1(state_pressed)
            && state != latin1(state_released)) {
            return std::unexpected(QStringLiteral(
                "field 'state' must be 'pressed' or 'released'"));
        }
        key.pressed = state == latin1(state_pressed);
        return finish(read, key);
    }
    if (event == latin1(event_modifiers)) {
        return finish(read,
                      ModifiersMessage {
                          .keyboardId = keyboardId,
                          .modifiers = read.modifiers(),
                      });
    }
    if (event == latin1(event_keymap)) {
        return finish(read,
                      KeymapMessage {
                          .keyboardId = keyboardId,
                          .keymap = read.nonEmptyString(field_keymap),
                      });
    }
    if (event == latin1(event_repeat_info)) {
        return finish(read,
                      RepeatInfoMessage {
                          .keyboardId = keyboardId,
                          .repeatInfo = read.repeatInfo(),
                      });
    }
    return std::unexpected(QStringLiteral("unsupported event '%1'").arg(event));
}

} // namespace hyprcast::overlay
