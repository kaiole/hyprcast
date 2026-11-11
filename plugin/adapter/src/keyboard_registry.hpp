#pragma once

#include "hyprcast/config/config.hpp"
#include "hyprcast/log/log_sink.hpp"
#include "hyprcast/log/run_guarded.hpp"
#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/registry_snapshot.hpp"
#include "hyprcast/session/session.hpp"
#include "keyboard_info.hpp"

#include <hyprland/src/helpers/memory/Memory.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hyprcast {

class keyboard_registry {
public:
    // Reports keyboards and their events to `events`. `log` and `events`
    // must outlive the registry.
    keyboard_registry(log_sink& log, session& events);

    // Listeners capture `this`, so the registry must never move.
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
        hyprcast::run_guarded(
            *log_,
            context,
            std::forward<Function>(function),
            [this] noexcept { session_->disconnect_client(); });
    }

    void sync_keymap(const SP<IKeyboard>& keyboard);
    void remove_keyboard(keyboard_id id) noexcept;
    void unsubscribe_listeners(keyboard_info& info) noexcept;

    log_sink* log_;
    session* session_;
    keyboard_id next_id_ = 1;
    std::vector<std::unique_ptr<keyboard_info>> keyboard_registry_;

    CHyprSignalListener layout_listener_;
};

} // namespace hyprcast
