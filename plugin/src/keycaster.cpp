#include "keycaster.hpp"

#include "hyprcast/log/run_guarded.hpp"
#include "hyprcast/server/event_server.hpp"
#include "hyprland/keyboard_registry.hpp"
#include "hyprland/plugin_config.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>

#include <cstdlib>
#include <stdexcept>

namespace hyprcast {

keycaster::keycaster()
    : session_(g_pCompositor->m_wlEventLoop,
               event_socket_dir(std::getenv("XDG_RUNTIME_DIR"),
                                g_pCompositor->m_instanceSignature),
               log_,
               [this] { return keyboard_registry_.get_registry_snapshot(); }),
      keyboard_registry_(log_, session_),
      config_(log_, make_plugin_config_callbacks()) {
    keyboard_registry_.update_subscriptions(config_.get_accepted_config());
}

void keycaster::add_keyboard(const SP<IKeyboard>& keyboard) {
    if (shutdown_) {
        return;
    }

    run_guarded(
        log_,
        "Cannot register keyboard; shutting down plugin",
        [this, &keyboard] {
            keyboard_registry_.add_keyboard(keyboard);
            keyboard_registry_.update_subscriptions(
                config_.get_accepted_config());
        },
        [this] noexcept { shutdown(); });
}

void keycaster::set_pause(bool paused) {
    if (shutdown_) {
        throw std::runtime_error("Hyprcast is stopped; reload the plugin");
    }

    session_.set_paused(paused);
}

bool keycaster::is_paused() const noexcept {
    return session_.paused();
}

plugin_config::callbacks keycaster::make_plugin_config_callbacks() {
    return {
        .on_config_reload =
            [this] {
                if (shutdown_) {
                    return;
                }

                keyboard_registry_.update_subscriptions(
                    config_.get_accepted_config());
            },
        .on_exception = [this] noexcept { shutdown(); },
    };
}

void keycaster::shutdown() noexcept {
    if (shutdown_) {
        return;
    }
    log_.notify_failure(
        "Plugin stopped after an internal failure. Unload and reload "
        "hyprcast; see Hyprland logs for details.");
    keyboard_registry_.shutdown();
    session_.shutdown();
    shutdown_ = true;
}

} // namespace hyprcast
