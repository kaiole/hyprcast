#include "KeyboardInterpreter.hpp"

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <type_traits>
#include <utility>

namespace Hyprcast::Overlay {
    namespace {
        struct ContextDeleter {
            void operator()(xkb_context* value) const noexcept {
                if (value) {
                    xkb_context_unref(value);
                }
            }
        };
        struct KeymapDeleter {
            void operator()(xkb_keymap* value) const noexcept {
                if (value) {
                    xkb_keymap_unref(value);
                }
            }
        };
        struct StateDeleter {
            void operator()(xkb_state* value) const noexcept {
                if (value) {
                    xkb_state_unref(value);
                }
            }
        };

        using ContextPtr = std::unique_ptr<xkb_context, ContextDeleter>;
        using KeymapPtr  = std::unique_ptr<xkb_keymap, KeymapDeleter>;
        using StatePtr   = std::unique_ptr<xkb_state, StateDeleter>;

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
            std::uint32_t                       id = 0;
            QString                             name;
            std::uint32_t                       depressed   = 0;
            std::uint32_t                       latched     = 0;
            std::uint32_t                       locked      = 0;
            std::uint32_t                       group       = 0;
            std::uint32_t                       repeatRate  = 0;
            std::uint32_t                       repeatDelay = 0;
            KeymapPtr                           keymap;
            StatePtr                            state;
            std::map<std::uint32_t, PressedKey> pressed;
        };

        ContextPtr                        context{xkb_context_new(XKB_CONTEXT_NO_FLAGS)};
        std::map<std::uint32_t, Keyboard> keyboards;
        bool                              paused = false;

        static void                       applyMask(Keyboard& keyboard) {
            if (!keyboard.state || !keyboard.keymap) {
                return;
            }

            const auto layoutCount = xkb_keymap_num_layouts(keyboard.keymap.get());
            const auto layout      = layoutCount > 0 ? keyboard.group % layoutCount : 0;
            xkb_state_update_mask(keyboard.state.get(), keyboard.depressed, keyboard.latched, keyboard.locked, 0, 0, layout);
        }

        static bool installKeymap(xkb_context* context, Keyboard& keyboard, const QString& text, QString* error) {
            keyboard.pressed.clear();
            keyboard.state.reset();
            keyboard.keymap.reset();

            if (!context) {
                if (error) {
                    *error = QStringLiteral("cannot create XKB context");
                }
                return false;
            }
            if (text.contains(QChar::Null)) {
                if (error) {
                    *error = QStringLiteral("keymap contains a NUL character");
                }
                return false;
            }

            const QByteArray utf8 = text.toUtf8();
            keyboard.keymap.reset(xkb_keymap_new_from_string(context, utf8.constData(), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS));
            if (!keyboard.keymap) {
                if (error) {
                    *error = QStringLiteral("XKB rejected the keyboard keymap");
                }
                return false;
            }

            keyboard.state.reset(xkb_state_new(keyboard.keymap.get()));
            if (!keyboard.state) {
                keyboard.keymap.reset();
                if (error) {
                    *error = QStringLiteral("cannot create XKB state for the keyboard keymap");
                }
                return false;
            }

            applyMask(keyboard);
            return true;
        }

        static KeyboardInterpreter::Clock::duration repeatInterval(std::uint32_t rate) {
            if (rate == 0) {
                return KeyboardInterpreter::Clock::duration::zero();
            }
            const auto interval = std::chrono::duration_cast<KeyboardInterpreter::Clock::duration>(std::chrono::duration<double>{1.0 / rate});
            return std::max(interval, KeyboardInterpreter::Clock::duration{1});
        }

        static void configureRepeat(Keyboard& keyboard, PressedKey& key, TimePoint now) {
            if (!key.repeatable || keyboard.repeatRate == 0 || !keyboard.state) {
                key.repeatDeadline.reset();
                key.repeatInterval = KeyboardInterpreter::Clock::duration::zero();
                return;
            }
            key.repeatInterval = repeatInterval(keyboard.repeatRate);
            key.repeatDeadline = now + std::chrono::milliseconds(keyboard.repeatDelay);
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
            keyboard.depressed = event.depressed;
            keyboard.latched   = event.latched;
            keyboard.locked    = event.locked;
            keyboard.group     = event.group;
            applyMask(keyboard);
        }

        void resetPressed(Keyboard& keyboard) {
            keyboard.pressed.clear();
            if (keyboard.keymap) {
                keyboard.state.reset(xkb_state_new(keyboard.keymap.get()));
                applyMask(keyboard);
            }
        }

        std::vector<InterpretedAction> onKey(const KeyMessage& event, TimePoint now, QString* error) {
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
            if (event.keycode > std::numeric_limits<xkb_keycode_t>::max() - 8) {
                if (error) {
                    *error = QStringLiteral("keycode overflows the XKB evdev-to-keycode offset");
                }
                return actions;
            }

            const auto xkbKey = static_cast<xkb_keycode_t>(event.keycode + 8);
            if (xkbKey < xkb_keymap_min_keycode(keyboard.keymap.get()) || xkbKey > xkb_keymap_max_keycode(keyboard.keymap.get())) {
                if (error) {
                    *error = QStringLiteral("keycode %1 is outside the active XKB keymap").arg(event.keycode);
                }
                return actions;
            }

            if (event.pressed) {
                if (keyboard.pressed.contains(event.keycode)) {
                    return actions;
                }

                // Hyprland forwards the evdev keycode; XKB keycodes are evdev + 8.
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
                    for (auto& [heldKeycode, held] : keyboard.pressed) {
                        Q_UNUSED(heldKeycode);
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
                    configureRepeat(keyboard, pressed, now);
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
            for (auto& [keyboardId, keyboard] : keyboards) {
                Q_UNUSED(keyboardId);
                for (auto& [keycode, key] : keyboard.pressed) {
                    Q_UNUSED(keycode);
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
            for (const auto& [keyboardId, keyboard] : keyboards) {
                Q_UNUSED(keyboardId);
                for (const auto& [keycode, key] : keyboard.pressed) {
                    Q_UNUSED(keycode);
                    if (key.repeatDeadline && *key.repeatDeadline < next) {
                        next = *key.repeatDeadline;
                    }
                }
            }
            return next;
        }

        QString heldModifiers() const {
            QStringList labels;
            for (const auto& [keyboardId, keyboard] : keyboards) {
                Q_UNUSED(keyboardId);
                for (const auto& [keycode, key] : keyboard.pressed) {
                    Q_UNUSED(keycode);
                    if (key.modifier && !labels.contains(key.modifierLabel)) {
                        labels.push_back(key.modifierLabel);
                    }
                }
            }
            return labels.join(QStringLiteral(" + "));
        }
    };

    KeyboardInterpreter::KeyboardInterpreter() : m_impl(std::make_unique<Impl>()) {}
    KeyboardInterpreter::~KeyboardInterpreter()                                                   = default;
    KeyboardInterpreter::KeyboardInterpreter(KeyboardInterpreter&&) noexcept                      = default;
    KeyboardInterpreter&           KeyboardInterpreter::operator=(KeyboardInterpreter&&) noexcept = default;

    std::vector<InterpretedAction> KeyboardInterpreter::process(const ProtocolMessage& message, Clock::time_point now, QString* error) {
        if (error) {
            error->clear();
        }
        if (!m_impl || !m_impl->context) {
            if (error) {
                *error = QStringLiteral("cannot initialize XKB context");
            }
            return {};
        }

        return std::visit(
            [this, now, error](const auto& event) -> std::vector<InterpretedAction> {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<T, CastingStateMessage>) {
                    m_impl->paused = event.paused;
                    if (event.paused) {
                        for (auto& [id, keyboard] : m_impl->keyboards) {
                            Q_UNUSED(id);
                            m_impl->resetPressed(keyboard);
                        }
                    }
                } else if constexpr (std::is_same_v<T, KeyboardSnapshotMessage>) {
                    m_impl->keyboards.erase(event.id);
                    Impl::Keyboard keyboard;
                    keyboard.id          = event.id;
                    keyboard.name        = event.name;
                    keyboard.depressed   = event.depressed;
                    keyboard.latched     = event.latched;
                    keyboard.locked      = event.locked;
                    keyboard.group       = event.group;
                    keyboard.repeatRate  = event.repeatRate;
                    keyboard.repeatDelay = event.repeatDelay;
                    if (!Impl::installKeymap(m_impl->context.get(), keyboard, event.keymap, error)) {
                        return {};
                    }
                    m_impl->keyboards.insert_or_assign(event.id, std::move(keyboard));
                } else if constexpr (std::is_same_v<T, UnsubscribeKeyboardMessage>) {
                    m_impl->keyboards.erase(event.id);
                } else if constexpr (std::is_same_v<T, KeyMessage>) {
                    return m_impl->onKey(event, now, error);
                } else if constexpr (std::is_same_v<T, ModifiersMessage>) {
                    const auto keyboard = m_impl->keyboards.find(event.keyboardId);
                    if (keyboard != m_impl->keyboards.end()) {
                        Impl::applyAuthoritativeModifiers(keyboard->second, event);
                    }
                } else if constexpr (std::is_same_v<T, KeymapMessage>) {
                    const auto keyboard = m_impl->keyboards.find(event.keyboardId);
                    if (keyboard != m_impl->keyboards.end()) {
                        Impl::installKeymap(m_impl->context.get(), keyboard->second, event.keymap, error);
                    }
                } else if constexpr (std::is_same_v<T, RepeatInfoMessage>) {
                    const auto keyboard = m_impl->keyboards.find(event.keyboardId);
                    if (keyboard != m_impl->keyboards.end()) {
                        keyboard->second.repeatRate  = event.rate;
                        keyboard->second.repeatDelay = event.delay;
                        for (auto& [keycode, key] : keyboard->second.pressed) {
                            Q_UNUSED(keycode);
                            Impl::configureRepeat(keyboard->second, key, now);
                        }
                    }
                }
                return {};
            },
            message);
    }

    std::vector<InterpretedAction> KeyboardInterpreter::advance(Clock::time_point now) {
        return m_impl ? m_impl->advance(now) : std::vector<InterpretedAction>{};
    }

    KeyboardInterpreter::Clock::time_point KeyboardInterpreter::nextRepeatDeadline() const noexcept {
        return m_impl ? m_impl->nextRepeatDeadline() : Clock::time_point::max();
    }

    QString KeyboardInterpreter::heldModifiers() const {
        return m_impl ? m_impl->heldModifiers() : QString{};
    }

    void KeyboardInterpreter::reset() noexcept {
        if (!m_impl) {
            return;
        }
        m_impl->keyboards.clear();
        m_impl->paused = false;
    }
} // namespace Hyprcast::Overlay
