#include "KeyboardInterpreter.hpp"

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>

namespace Hyprcast::Overlay {
    namespace {
        // Stateless deleter calling an xkbcommon unref function. unique_ptr never
        // invokes its deleter on null, so no null check is needed here.
        template <auto Unref>
        struct XkbDeleter {
            template <typename T>
            void operator()(T* value) const noexcept {
                Unref(value);
            }
        };

        using ContextPtr = std::unique_ptr<xkb_context, XkbDeleter<&xkb_context_unref>>;
        using KeymapPtr  = std::unique_ptr<xkb_keymap, XkbDeleter<&xkb_keymap_unref>>;
        using StatePtr   = std::unique_ptr<xkb_state, XkbDeleter<&xkb_state_unref>>;

        // Hyprland forwards evdev keycodes; XKB keycodes are offset by 8.
        constexpr std::uint32_t EvdevToXkbOffset = 8;

        struct ModifierDefinition {
            const char* xkbName;
            const char* label;
        };

        constexpr std::array ModifierDefinitions{
            ModifierDefinition{"Shift", "Shift"}, ModifierDefinition{"Control", "Ctrl"}, ModifierDefinition{"Mod1", "Alt"},
            ModifierDefinition{"Mod4", "Super"},  ModifierDefinition{"Mod5", "AltGr"},
        };

        bool isControlText(const QString& text) {
            for (const QChar character : text) {
                if (character.category() == QChar::Other_Control) {
                    return true;
                }
            }
            return false;
        }

        QString keyLabel(xkb_keysym_t symbol) {
            switch (symbol) {
                case XKB_KEY_Return: return QStringLiteral("Enter");
                case XKB_KEY_KP_Enter: return QStringLiteral("Enter");
                case XKB_KEY_Escape: return QStringLiteral("Esc");
                case XKB_KEY_Tab: return QStringLiteral("Tab");
                case XKB_KEY_ISO_Left_Tab: return QStringLiteral("Tab");
                case XKB_KEY_BackSpace: return QStringLiteral("Backspace");
                case XKB_KEY_Delete: return QStringLiteral("Delete");
                case XKB_KEY_Insert: return QStringLiteral("Insert");
                case XKB_KEY_Home: return QStringLiteral("Home");
                case XKB_KEY_End: return QStringLiteral("End");
                case XKB_KEY_Page_Up: return QStringLiteral("Page Up");
                case XKB_KEY_Page_Down: return QStringLiteral("Page Down");
                case XKB_KEY_Left: return QStringLiteral("Left");
                case XKB_KEY_Right: return QStringLiteral("Right");
                case XKB_KEY_Up: return QStringLiteral("Up");
                case XKB_KEY_Down: return QStringLiteral("Down");
                case XKB_KEY_space: return QStringLiteral("Space");
                case XKB_KEY_Print: return QStringLiteral("Print Screen");
                case XKB_KEY_Pause: return QStringLiteral("Pause");
                case XKB_KEY_Menu: return QStringLiteral("Menu");
                case XKB_KEY_Shift_L:
                case XKB_KEY_Shift_R: return QStringLiteral("Shift");
                case XKB_KEY_Control_L:
                case XKB_KEY_Control_R: return QStringLiteral("Ctrl");
                case XKB_KEY_Alt_L:
                case XKB_KEY_Alt_R: return QStringLiteral("Alt");
                case XKB_KEY_Super_L:
                case XKB_KEY_Super_R: return QStringLiteral("Super");
                case XKB_KEY_ISO_Level3_Shift: return QStringLiteral("AltGr");
                default: break;
            }

            if (symbol >= XKB_KEY_F1 && symbol <= XKB_KEY_F35) {
                return QStringLiteral("F%1").arg(symbol - XKB_KEY_F1 + 1);
            }

            const xkb_keysym_t unicode = xkb_keysym_to_utf32(symbol);
            if (unicode >= 0x20 && unicode <= 0x10FFFF && !(unicode >= 0xD800 && unicode <= 0xDFFF)) {
                const char32_t codepoint = static_cast<char32_t>(unicode);
                const QString  result    = QString::fromUcs4(&codepoint, 1);
                return result.size() == 1 ? result.toUpper() : result;
            }

            std::array<char, 128> name{};
            const int             length = xkb_keysym_get_name(symbol, name.data(), name.size());
            if (length <= 0) {
                return QStringLiteral("Key");
            }

            QString result = QString::fromLatin1(name.data(), length);
            if (result.startsWith(QStringLiteral("XF86"))) {
                result.remove(0, 4);
            }
            result.replace(QLatin1Char('_'), QLatin1Char(' '));
            return result;
        }

        QString keyText(xkb_state* state, xkb_keycode_t key) {
            const int required = xkb_state_key_get_utf8(state, key, nullptr, 0);
            if (required <= 0) {
                return {};
            }

            QByteArray buffer(required + 1, '\0');
            const int  written = xkb_state_key_get_utf8(state, key, buffer.data(), static_cast<std::size_t>(buffer.size()));
            if (written <= 0 || written > required) {
                return {};
            }
            return QString::fromUtf8(buffer.constData(), written);
        }
    } // namespace

    struct KeyboardInterpreter::Impl {
        using TimePoint = KeyboardInterpreter::Clock::time_point;

        struct PressedKey {
            InterpretedAction                    action;
            QString                              modifierLabel;
            bool                                 modifier   = false;
            bool                                 used       = false;
            bool                                 repeatable = false;
            std::optional<TimePoint>             repeatDeadline;
            KeyboardInterpreter::Clock::duration repeatInterval{};
            std::uint32_t                        repeatCount = 0;
        };

        struct Keyboard {
            SModifiers                          modifiers;
            SRepeatInfo                         repeatInfo;
            KeymapPtr                           keymap;
            StatePtr                            state;
            std::map<std::uint32_t, PressedKey> pressed;
        };

        ContextPtr                        context{xkb_context_new(XKB_CONTEXT_NO_FLAGS)};
        std::map<std::uint32_t, Keyboard> keyboards;
        bool                              paused         = false;
        bool                              repeatsEnabled = true;

        static void                       applyMask(Keyboard& keyboard) {
            if (!keyboard.state || !keyboard.keymap) {
                return;
            }

            const auto  layoutCount = xkb_keymap_num_layouts(keyboard.keymap.get());
            const auto& mods        = keyboard.modifiers;
            const auto  layout      = layoutCount > 0 ? mods.group % layoutCount : 0;
            xkb_state_update_mask(keyboard.state.get(), mods.depressed, mods.latched, mods.locked, 0, 0, layout);
        }

        static std::expected<void, QString> installKeymap(xkb_context* context, Keyboard& keyboard, const QString& text) {
            keyboard.pressed.clear();
            keyboard.state.reset();
            keyboard.keymap.reset();

            if (!context) {
                return std::unexpected(QStringLiteral("cannot create XKB context"));
            }
            if (text.contains(QChar::Null)) {
                return std::unexpected(QStringLiteral("keymap contains a NUL character"));
            }

            const QByteArray utf8 = text.toUtf8();
            keyboard.keymap.reset(xkb_keymap_new_from_string(context, utf8.constData(), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS));
            if (!keyboard.keymap) {
                return std::unexpected(QStringLiteral("XKB rejected the keyboard keymap"));
            }

            keyboard.state.reset(xkb_state_new(keyboard.keymap.get()));
            if (!keyboard.state) {
                keyboard.keymap.reset();
                return std::unexpected(QStringLiteral("cannot create XKB state for the keyboard keymap"));
            }

            applyMask(keyboard);
            return {};
        }

        static KeyboardInterpreter::Clock::duration repeatInterval(std::uint32_t rate) {
            if (rate == 0) {
                return KeyboardInterpreter::Clock::duration::zero();
            }
            const auto interval = std::chrono::duration_cast<KeyboardInterpreter::Clock::duration>(std::chrono::duration<double>{1.0 / rate});
            return std::max(interval, KeyboardInterpreter::Clock::duration{1});
        }

        static void configureRepeat(Keyboard& keyboard, PressedKey& key, TimePoint now, bool enabled) {
            if (!enabled || !key.repeatable || keyboard.repeatInfo.rate == 0 || !keyboard.state) {
                key.repeatDeadline.reset();
                key.repeatInterval = KeyboardInterpreter::Clock::duration::zero();
                return;
            }
            key.repeatInterval = repeatInterval(keyboard.repeatInfo.rate);
            key.repeatDeadline = now + std::chrono::milliseconds(keyboard.repeatInfo.delay);
        }

        static xkb_mod_mask_t modifierMask(xkb_keymap* keymap, const char* name) {
            const auto index = xkb_keymap_mod_get_index(keymap, name);
            if (index == XKB_MOD_INVALID || index >= sizeof(xkb_mod_mask_t) * 8) {
                return 0;
            }
            return static_cast<xkb_mod_mask_t>(xkb_mod_mask_t{1} << index);
        }

        static QStringList activeModifiers(const Keyboard& keyboard, xkb_keycode_t key, bool includeConsumed) {
            QStringList result;
            if (!keyboard.state || !keyboard.keymap) {
                return result;
            }

            const auto effective = xkb_state_serialize_mods(keyboard.state.get(), XKB_STATE_MODS_EFFECTIVE);
            const auto consumed  = xkb_state_key_get_consumed_mods2(keyboard.state.get(), key, XKB_CONSUMED_MODE_XKB);
            for (const auto& definition : ModifierDefinitions) {
                const auto mask = modifierMask(keyboard.keymap.get(), definition.xkbName);
                if (mask == 0 || (effective & mask) == 0 || (!includeConsumed && (consumed & mask) != 0)) {
                    continue;
                }
                result.push_back(QString::fromLatin1(definition.label));
            }
            return result;
        }

        static QString modifierLabel(xkb_keysym_t symbol) {
            switch (symbol) {
                case XKB_KEY_Shift_L:
                case XKB_KEY_Shift_R: return QStringLiteral("Shift");
                case XKB_KEY_Control_L:
                case XKB_KEY_Control_R: return QStringLiteral("Ctrl");
                case XKB_KEY_Alt_L:
                case XKB_KEY_Alt_R: return QStringLiteral("Alt");
                case XKB_KEY_Super_L:
                case XKB_KEY_Super_R: return QStringLiteral("Super");
                case XKB_KEY_ISO_Level3_Shift: return QStringLiteral("AltGr");
                case XKB_KEY_Meta_L:
                case XKB_KEY_Meta_R: return QStringLiteral("Meta");
                case XKB_KEY_Hyper_L:
                case XKB_KEY_Hyper_R: return QStringLiteral("Hyper");
                default: return {};
            }
        }

        static QString textForKey(xkb_state* state, xkb_keycode_t key) {
            const QString text = keyText(state, key);
            return text.isEmpty() || isControlText(text) ? QString{} : text;
        }

        static void applyAuthoritativeModifiers(Keyboard& keyboard, const ModifiersMessage& event) {
            keyboard.modifiers = event.modifiers;
            applyMask(keyboard);
        }

        void resetPressed(Keyboard& keyboard) {
            keyboard.pressed.clear();
            if (keyboard.keymap) {
                keyboard.state.reset(xkb_state_new(keyboard.keymap.get()));
                applyMask(keyboard);
            }
        }

        std::expected<std::vector<InterpretedAction>, QString> onKey(const KeyMessage& event, TimePoint now) {
            std::vector<InterpretedAction> actions;
            if (paused) {
                return actions;
            }

            const auto keyboardIt = keyboards.find(event.keyboardId);
            if (keyboardIt == keyboards.end()) {
                return actions;
            }
            auto& keyboard = keyboardIt->second;
            if (!keyboard.state || !keyboard.keymap) {
                return actions;
            }
            if (event.keycode > std::numeric_limits<xkb_keycode_t>::max() - EvdevToXkbOffset) {
                return std::unexpected(QStringLiteral("keycode overflows the XKB evdev-to-keycode offset"));
            }

            const xkb_keycode_t xkbKey = event.keycode + EvdevToXkbOffset;
            if (xkbKey < xkb_keymap_min_keycode(keyboard.keymap.get()) || xkbKey > xkb_keymap_max_keycode(keyboard.keymap.get())) {
                return std::unexpected(QStringLiteral("keycode %1 is outside the active XKB keymap").arg(event.keycode));
            }

            if (event.pressed) {
                if (keyboard.pressed.contains(event.keycode)) {
                    return actions;
                }

                // Apply the key locally, then reconcile with the compositor's authoritative
                // modifier masks when its following modifiers event arrives.
                xkb_state_update_key(keyboard.state.get(), xkbKey, XKB_KEY_DOWN);
                const auto symbol = xkb_state_key_get_one_sym(keyboard.state.get(), xkbKey);
                const auto mod    = modifierLabel(symbol);

                PressedKey pressed;
                pressed.action.keyboardId  = event.keyboardId;
                pressed.action.keycode     = event.keycode;
                pressed.action.eventTimeMs = event.timeMs;
                pressed.action.generatedAt = now;

                if (!mod.isEmpty()) {
                    pressed.modifier      = true;
                    pressed.modifierLabel = mod;
                    pressed.action.kind   = InterpretedActionKind::Key;
                    pressed.action.key    = mod;
                } else {
                    for (auto& held : keyboard.pressed | std::views::values) {
                        if (held.modifier) {
                            held.used = true;
                        }
                    }
                    const auto    allModifiers   = activeModifiers(keyboard, xkbKey, true);
                    const auto    unconsumedMods = activeModifiers(keyboard, xkbKey, false);
                    const QString key            = keyLabel(symbol);
                    const QString text           = textForKey(keyboard.state.get(), xkbKey);
                    pressed.action.modifiers     = allModifiers;
                    if (!text.isEmpty() && unconsumedMods.isEmpty()) {
                        pressed.action.kind = InterpretedActionKind::Text;
                        pressed.action.text = text;
                    } else if (!allModifiers.isEmpty()) {
                        pressed.action.kind = InterpretedActionKind::Chord;
                        pressed.action.key  = key;
                    } else if (!text.isEmpty()) {
                        pressed.action.kind = InterpretedActionKind::Text;
                        pressed.action.text = text;
                    } else {
                        pressed.action.kind = InterpretedActionKind::Key;
                        pressed.action.key  = key;
                    }
                    pressed.repeatable = xkb_keymap_key_repeats(keyboard.keymap.get(), xkbKey) != 0;
                    configureRepeat(keyboard, pressed, now, repeatsEnabled);
                    actions.push_back(pressed.action);
                }

                keyboard.pressed.emplace(event.keycode, std::move(pressed));
                return actions;
            }

            xkb_state_update_key(keyboard.state.get(), xkbKey, XKB_KEY_UP);
            const auto pressed = keyboard.pressed.find(event.keycode);
            if (pressed == keyboard.pressed.end()) {
                return actions;
            }

            if (pressed->second.modifier && !pressed->second.used) {
                auto action        = pressed->second.action;
                action.generatedAt = now;
                action.eventTimeMs = event.timeMs;
                action.kind        = InterpretedActionKind::Key;
                action.key         = pressed->second.modifierLabel;
                actions.push_back(std::move(action));
            }
            keyboard.pressed.erase(pressed);
            return actions;
        }

        std::vector<InterpretedAction> advance(TimePoint now) {
            std::vector<InterpretedAction> actions;
            constexpr int                  MaxCatchUpRepeats = 32;
            for (auto& keyboard : keyboards | std::views::values) {
                for (auto& key : keyboard.pressed | std::views::values) {
                    if (!key.repeatDeadline || now < *key.repeatDeadline) {
                        continue;
                    }

                    int emitted = 0;
                    while (key.repeatDeadline && *key.repeatDeadline <= now && emitted < MaxCatchUpRepeats) {
                        ++key.repeatCount;
                        auto action        = key.action;
                        action.repeated    = true;
                        action.repeatCount = key.repeatCount;
                        action.generatedAt = *key.repeatDeadline;
                        actions.push_back(std::move(action));
                        *key.repeatDeadline += key.repeatInterval;
                        ++emitted;
                    }
                    if (key.repeatDeadline && *key.repeatDeadline <= now) {
                        *key.repeatDeadline = now + key.repeatInterval;
                    }
                }
            }
            return actions;
        }

        TimePoint nextRepeatDeadline() const noexcept {
            auto next = TimePoint::max();
            for (const auto& keyboard : keyboards | std::views::values) {
                for (const auto& key : keyboard.pressed | std::views::values) {
                    if (key.repeatDeadline && *key.repeatDeadline < next) {
                        next = *key.repeatDeadline;
                    }
                }
            }
            return next;
        }

        QString heldModifiers() const {
            QStringList labels;
            for (const auto& keyboard : keyboards | std::views::values) {
                for (const auto& key : keyboard.pressed | std::views::values) {
                    if (key.modifier && !labels.contains(key.modifierLabel)) {
                        labels.push_back(key.modifierLabel);
                    }
                }
            }
            return labels.join(QStringLiteral(" + "));
        }

        QStringList heldKeys() const {
            QStringList labels;
            for (const HeldKey& item : heldKeyItems()) {
                labels.push_back(item.kind == QStringLiteral("text") ? item.text : item.identity);
            }
            return labels;
        }

        std::vector<HeldKey> heldKeyItems() const {
            std::vector<HeldKey> items;
            for (const auto& keyboard : keyboards | std::views::values) {
                for (const auto& key : keyboard.pressed | std::views::values) {
                    if (key.modifier) {
                        items.push_back({QStringLiteral("modifier"), key.modifierLabel, {}});
                    } else if (key.action.kind == InterpretedActionKind::Text) {
                        items.push_back({QStringLiteral("text"), {}, key.action.text});
                    } else if (!key.action.key.isEmpty()) {
                        items.push_back({QStringLiteral("key"), key.action.key, {}});
                    }
                }
            }
            return items;
        }
    };

    KeyboardInterpreter::KeyboardInterpreter() : m_impl(std::make_unique<Impl>()) {}
    KeyboardInterpreter::~KeyboardInterpreter() = default;

    std::expected<std::vector<InterpretedAction>, QString> KeyboardInterpreter::process(const ProtocolMessage& message, Clock::time_point now) {
        using Result = std::expected<std::vector<InterpretedAction>, QString>;
        if (!m_impl->context) {
            return std::unexpected(QStringLiteral("cannot initialize XKB context"));
        }

        return std::visit(
            [this, now](const auto& event) -> Result {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<T, CastingStateMessage>) {
                    m_impl->paused = event.paused;
                    if (event.paused) {
                        for (auto& keyboard : m_impl->keyboards | std::views::values) {
                            m_impl->resetPressed(keyboard);
                        }
                    }
                } else if constexpr (std::is_same_v<T, KeyboardSnapshotMessage>) {
                    m_impl->keyboards.erase(event.id);
                    Impl::Keyboard keyboard{.modifiers = event.modifiers, .repeatInfo = event.repeatInfo};
                    if (auto installed = Impl::installKeymap(m_impl->context.get(), keyboard, event.keymap); !installed) {
                        return std::unexpected(installed.error());
                    }
                    m_impl->keyboards.insert_or_assign(event.id, std::move(keyboard));
                } else if constexpr (std::is_same_v<T, UnsubscribeKeyboardMessage>) {
                    m_impl->keyboards.erase(event.id);
                } else if constexpr (std::is_same_v<T, KeyMessage>) {
                    return m_impl->onKey(event, now);
                } else if constexpr (std::is_same_v<T, ModifiersMessage>) {
                    const auto keyboard = m_impl->keyboards.find(event.keyboardId);
                    if (keyboard != m_impl->keyboards.end()) {
                        Impl::applyAuthoritativeModifiers(keyboard->second, event);
                    }
                } else if constexpr (std::is_same_v<T, KeymapMessage>) {
                    const auto keyboard = m_impl->keyboards.find(event.keyboardId);
                    if (keyboard != m_impl->keyboards.end()) {
                        if (auto installed = Impl::installKeymap(m_impl->context.get(), keyboard->second, event.keymap); !installed) {
                            return std::unexpected(installed.error());
                        }
                    }
                } else if constexpr (std::is_same_v<T, RepeatInfoMessage>) {
                    const auto keyboard = m_impl->keyboards.find(event.keyboardId);
                    if (keyboard != m_impl->keyboards.end()) {
                        keyboard->second.repeatInfo = event.repeatInfo;
                        for (auto& key : keyboard->second.pressed | std::views::values) {
                            Impl::configureRepeat(keyboard->second, key, now, m_impl->repeatsEnabled);
                        }
                    }
                } else {
                    static_assert(false, "unhandled ProtocolMessage alternative");
                }
                return {};
            },
            message);
    }

    std::vector<InterpretedAction> KeyboardInterpreter::advance(Clock::time_point now) {
        return m_impl->advance(now);
    }

    KeyboardInterpreter::Clock::time_point KeyboardInterpreter::nextRepeatDeadline() const noexcept {
        return m_impl->nextRepeatDeadline();
    }

    QString KeyboardInterpreter::heldModifiers() const {
        return m_impl->heldModifiers();
    }

    QStringList KeyboardInterpreter::heldKeys() const {
        return m_impl->heldKeys();
    }

    std::vector<HeldKey> KeyboardInterpreter::heldKeyItems() const {
        return m_impl->heldKeyItems();
    }

    void KeyboardInterpreter::setRepeatsEnabled(bool enabled, Clock::time_point now) {
        if (m_impl->repeatsEnabled == enabled) {
            return;
        }
        m_impl->repeatsEnabled = enabled;
        for (auto& keyboard : m_impl->keyboards | std::views::values) {
            for (auto& key : keyboard.pressed | std::views::values) {
                Impl::configureRepeat(keyboard, key, now, enabled);
            }
        }
    }

    void KeyboardInterpreter::reset() noexcept {
        m_impl->keyboards.clear();
        m_impl->paused = false;
    }
} // namespace Hyprcast::Overlay
