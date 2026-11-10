#pragma once

#include "hyprland/KeyboardRegistry.hpp"
#include "hyprland/PluginConfig.hpp"
#include "ipc/EventServer.hpp"

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
    void request_registry_snapshot();
    keyboard_registry::callbacks make_registry_callbacks();
    plugin_config::callbacks make_plugin_config_callbacks();

    void shutdown() noexcept;

    event_server server_;
    keyboard_registry keyboard_registry_;
    plugin_config config_;

    bool shutdown_ = false;
    bool paused_ = true;
};

} // namespace hyprcast
