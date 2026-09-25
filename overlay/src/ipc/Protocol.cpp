#include "Protocol.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace Hyprcast::Overlay {
    namespace {
        bool fail(QString* error, const QString& reason) {
            if (error) {
                *error = reason;
            }
            return false;
        }

        bool requiredString(const QJsonObject& object, const QString& name, QString* result, QString* error) {
            const auto value = object.value(name);
            if (!value.isString()) {
                return fail(error, QStringLiteral("field '%1' must be a string").arg(name));
            }
            *result = value.toString();
            return true;
        }

        bool requiredBoolean(const QJsonObject& object, const QString& name, bool* result, QString* error) {
            const auto value = object.value(name);
            if (!value.isBool()) {
                return fail(error, QStringLiteral("field '%1' must be a boolean").arg(name));
            }
            *result = value.toBool();
            return true;
        }

        bool requiredUint32(const QJsonObject& object, const QString& name, std::uint32_t* result, QString* error) {
            const auto value = object.value(name);
            if (!value.isDouble()) {
                return fail(error, QStringLiteral("field '%1' must be an unsigned 32-bit integer").arg(name));
            }

            const double number = value.toDouble();
            if (!std::isfinite(number) || number < 0.0 || number > std::numeric_limits<std::uint32_t>::max() || std::floor(number) != number) {
                return fail(error, QStringLiteral("field '%1' is outside the unsigned 32-bit integer range").arg(name));
            }

            *result = static_cast<std::uint32_t>(number);
            return true;
        }

        bool requiredKeyboardId(const QJsonObject& object, std::uint32_t* result, QString* error) {
            if (!requiredUint32(object, QStringLiteral("keyboard_id"), result, error)) {
                return false;
            }
            if (*result == 0) {
                return fail(error, QStringLiteral("field 'keyboard_id' must be greater than zero for this event"));
            }
            return true;
        }

        bool requiredRepeatInfo(const QJsonObject& object, std::uint32_t* rate, std::uint32_t* delay, QString* error) {
            if (!requiredUint32(object, QStringLiteral("rate"), rate, error) || !requiredUint32(object, QStringLiteral("delay"), delay, error)) {
                return false;
            }
            if (*rate > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || *delay > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                return fail(error, QStringLiteral("repeat settings exceed the signed integer range"));
            }
            return true;
        }
    } // namespace

    bool parseMessage(const QByteArray& jsonLine, ProtocolMessage* message, QString* error) {
        if (error) {
            error->clear();
        }
        if (!message) {
            return fail(error, QStringLiteral("internal error: message output is null"));
        }

        QJsonParseError parseError{};
        const auto      document = QJsonDocument::fromJson(jsonLine, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return fail(error, QStringLiteral("invalid JSON object: %1").arg(parseError.errorString()));
        }

        const QJsonObject object = document.object();
        QString           event;
        if (!requiredString(object, QStringLiteral("event"), &event, error)) {
            return false;
        }

        std::uint32_t keyboardId = 0;
        if (event == QStringLiteral("casting_state")) {
            if (!requiredUint32(object, QStringLiteral("keyboard_id"), &keyboardId, error)) {
                return false;
            }
            if (keyboardId != 0) {
                return fail(error, QStringLiteral("casting_state requires keyboard_id 0"));
            }
            bool paused = false;
            if (!requiredBoolean(object, QStringLiteral("paused"), &paused, error)) {
                return false;
            }
            *message = CastingStateMessage{.paused = paused};
            return true;
        }

        if (event == QStringLiteral("subscribe_keyboard")) {
            KeyboardSnapshotMessage snapshot;
            if (!requiredKeyboardId(object, &snapshot.id, error) || !requiredString(object, QStringLiteral("name"), &snapshot.name, error) ||
                !requiredUint32(object, QStringLiteral("depressed"), &snapshot.depressed, error) || !requiredUint32(object, QStringLiteral("latched"), &snapshot.latched, error) ||
                !requiredUint32(object, QStringLiteral("locked"), &snapshot.locked, error) || !requiredUint32(object, QStringLiteral("group"), &snapshot.group, error) ||
                !requiredString(object, QStringLiteral("keymap"), &snapshot.keymap, error) || !requiredRepeatInfo(object, &snapshot.repeatRate, &snapshot.repeatDelay, error)) {
                return false;
            }
            if (snapshot.keymap.isEmpty()) {
                return fail(error, QStringLiteral("field 'keymap' must not be empty"));
            }
            *message = std::move(snapshot);
            return true;
        }

        if (event == QStringLiteral("unsubscribe_keyboard")) {
            if (!requiredKeyboardId(object, &keyboardId, error)) {
                return false;
            }
            *message = UnsubscribeKeyboardMessage{.id = keyboardId};
            return true;
        }

        if (!requiredKeyboardId(object, &keyboardId, error)) {
            return false;
        }

        if (event == QStringLiteral("key")) {
            KeyMessage key;
            key.keyboardId = keyboardId;
            QString state;
            if (!requiredUint32(object, QStringLiteral("time_ms"), &key.timeMs, error) || !requiredUint32(object, QStringLiteral("keycode"), &key.keycode, error) ||
                !requiredString(object, QStringLiteral("state"), &state, error)) {
                return false;
            }
            if (state != QStringLiteral("pressed") && state != QStringLiteral("released")) {
                return fail(error, QStringLiteral("field 'state' must be 'pressed' or 'released'"));
            }
            key.pressed = state == QStringLiteral("pressed");
            *message    = key;
            return true;
        }

        if (event == QStringLiteral("modifiers")) {
            ModifiersMessage modifiers;
            modifiers.keyboardId = keyboardId;
            if (!requiredUint32(object, QStringLiteral("depressed"), &modifiers.depressed, error) ||
                !requiredUint32(object, QStringLiteral("latched"), &modifiers.latched, error) || !requiredUint32(object, QStringLiteral("locked"), &modifiers.locked, error) ||
                !requiredUint32(object, QStringLiteral("group"), &modifiers.group, error)) {
                return false;
            }
            *message = modifiers;
            return true;
        }

        if (event == QStringLiteral("keymap")) {
            KeymapMessage keymap;
            keymap.keyboardId = keyboardId;
            if (!requiredString(object, QStringLiteral("keymap"), &keymap.keymap, error)) {
                return false;
            }
            if (keymap.keymap.isEmpty()) {
                return fail(error, QStringLiteral("field 'keymap' must not be empty"));
            }
            *message = std::move(keymap);
            return true;
        }

        if (event == QStringLiteral("repeat_info")) {
            RepeatInfoMessage repeatInfo;
            repeatInfo.keyboardId = keyboardId;
            if (!requiredRepeatInfo(object, &repeatInfo.rate, &repeatInfo.delay, error)) {
                return false;
            }
            *message = repeatInfo;
            return true;
        }

        return fail(error, QStringLiteral("unsupported event '%1'").arg(event));
    }
} // namespace Hyprcast::Overlay
