#pragma once

#include "hyprcast/session/session.hpp"
#include "keyboard_registry.hpp"
#include "log.hpp"
#include "plugin_config.hpp"

#include <helpers/memory/Memory.hpp>

namespace hyprcast {

class keycaster {
public:
    keycaster();

    // Members hold callbacks that capture `this`, so the object must never
    // move.
    keycaster(const keycaster&) = delete;
    keycaster& operator=(const keycaster&) = delete;
    keycaster(keycaster&&) = delete;
    keycaster& operator=(keycaster&&) = delete;
    ~keycaster() = default;

    void add_keyboard(const SP<IKeyboard>& keyboard);

    void set_pause(bool paused);
    [[nodiscard]] bool is_paused() const noexcept;

private:
    plugin_config::callbacks make_plugin_config_callbacks();

    void shutdown() noexcept;

    // First, so that it outlives every member that reports through it.
    hyprland_log_sink log_;
    // Before the registry, which reports to it.
    session session_;
    keyboard_registry keyboard_registry_;
    plugin_config config_;

    bool shutdown_ = false;
};

} // namespace hyprcast
