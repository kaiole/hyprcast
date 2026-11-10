#pragma once

#include "CallbackBoundary.hpp"
#include "KeyboardInfo.hpp"
#include "hyprcast/config/Config.hpp"
#include "hyprcast/protocol/KeyEvent.hpp"
#include "hyprcast/protocol/KeyboardId.hpp"
#include "hyprcast/protocol/KeyboardSnapshot.hpp"
#include "hyprcast/protocol/Keymap.hpp"
#include "hyprcast/protocol/Modifiers.hpp"
#include "hyprcast/protocol/RegistrySnapshot.hpp"
#include "hyprcast/protocol/RepeatInfo.hpp"
#include "wayland/WlEventSource.hpp"

#include <hyprland/src/helpers/memory/Memory.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hyprcast {

class keyboard_registry {
public:
    using exception_cb = std::move_only_function<void() noexcept>;
    using subscription_cb = std::function<void(keyboard_snapshot)>;
    using unsubscription_cb = std::function<void(keyboard_id)>;
    using key_event_cb = std::function<void(keyboard_id, key_event)>;
    using modifiers_event_cb = std::function<void(keyboard_id, modifier_state)>;
    using keymap_event_cb = std::function<void(keyboard_id, keymap_text)>;
    using repeat_info_event_cb = std::function<void(keyboard_id, repeat_info)>;

    struct callbacks {
        exception_cb on_exception;
        subscription_cb on_subscribe;
        unsubscription_cb on_unsubscribe;
        key_event_cb on_key_event;
        modifiers_event_cb on_modifiers_event;
        keymap_event_cb on_keymap_event;
        repeat_info_event_cb on_repeat_info_event;
    };

    explicit keyboard_registry(callbacks cbs);

    // Listeners capture `this` and per-keyboard record pointers, so the
    // registry must never move.
    keyboard_registry(const keyboard_registry&) = delete;
    keyboard_registry& operator=(const keyboard_registry&) = delete;
    keyboard_registry(keyboard_registry&&) = delete;
    keyboard_registry& operator=(keyboard_registry&&) = delete;
    ~keyboard_registry() = default;

    void add_keyboard(const SP<IKeyboard>& keyboard);
    void update_subscriptions(const config& accepted_config);

    // Snapshots every subscribed keyboard and records each keymap as sent.
    [[nodiscard]] registry_snapshot get_registry_snapshot();

    void shutdown() noexcept;

private:
    template <typename Function>
    void run_guarded(std::string_view context, Function&& function) {
        hyprcast::run_guarded(context,
                              std::forward<Function>(function),
                              callbacks_.on_exception);
    }

    void sync_keymap(const SP<IKeyboard>& keyboard);
    void schedule_removal(keyboard_info& info) noexcept;
    void unsubscribe_listeners(keyboard_info& info) noexcept;

    callbacks callbacks_;
    keyboard_id next_id_ = 1;
    std::vector<std::unique_ptr<keyboard_info>> keyboard_registry_;

    CHyprSignalListener layout_listener_;
    unique_wl_event_source wl_idle_keyboard_removal_;
};

} // namespace hyprcast
