#pragma once

#include "hyprcast/log/log_sink.hpp"
#include "hyprland/plugin.hpp"

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <format>
#include <string_view>

namespace hyprcast {

// Reports to Hyprland's log and, for failures the user has to act on, as an
// on-screen notification. Stateless: it only uses Hyprland's globals and
// plugin_handle.
class hyprland_log_sink final : public log_sink {
private:
    void do_write(log_level level, std::string_view message) override {
        Log::logger->log(to_hyprland(level), "[hyprcast] {}", message);
    }

    void do_notify_failure(std::string_view message) override {
        HyprlandAPI::addNotification(plugin_handle,
                                     std::format("[hyprcast] {}", message),
                                     CHyprColor {1.0f, 0.2f, 0.2f, 1.0f},
                                     8000);
    }

    [[nodiscard]] static Hyprutils::CLI::eLogLevel
    to_hyprland(log_level level) noexcept {
        switch (level) {
        case log_level::trace: return Log::TRACE;
        case log_level::warning: return Log::WARN;
        case log_level::error: return Log::ERR;
        }
        return Log::ERR;
    }
};

} // namespace hyprcast
