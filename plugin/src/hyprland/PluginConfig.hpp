#pragma once

#include "CallbackBoundary.hpp"
#include "hyprcast/config/Config.hpp"

#include <hyprland/src/helpers/signal/Signal.hpp>

#include <functional>
#include <string_view>
#include <utility>

namespace hyprcast {

class plugin_config {
public:
    using config_reload_cb = std::function<void()>;
    using exception_cb = std::move_only_function<void() noexcept>;

    struct callbacks {
        config_reload_cb on_config_reload;
        exception_cb on_exception;
    };

    explicit plugin_config(callbacks cbs);
    // Unregisters hyprcast.configure, whose C callback reaches this object.
    ~plugin_config();

    plugin_config(const plugin_config&) = delete;
    plugin_config& operator=(const plugin_config&) = delete;
    plugin_config(plugin_config&&) = delete;
    plugin_config& operator=(plugin_config&&) = delete;

    [[nodiscard]] const config& get_accepted_config() const noexcept {
        return config_.accepted();
    }

private:
    static int configure(lua_State* lua) noexcept;

    void handle_config_reload();

    template <typename Function>
    void run_guarded(std::string_view context, Function&& function) {
        hyprcast::run_guarded(context,
                              std::forward<Function>(function),
                              callbacks_.on_exception);
    }

    // Callbacks are declared before the listeners that invoke them.
    callbacks callbacks_;
    config_state config_;
    CHyprSignalListener pre_reload_, reloaded_;
};

} // namespace hyprcast
