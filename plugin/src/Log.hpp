#pragma once

#include "hyprland/Plugin.hpp"

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <format>
#include <string_view>
#include <utility>

namespace hyprcast {

template <typename... Args>
void log_message(Hyprutils::CLI::eLogLevel level,
                 std::format_string<Args...> fmt,
                 Args&&... args) noexcept {
    try {
        Log::logger->log(level,
                         "[hyprcast] {}",
                         std::format(fmt, std::forward<Args>(args)...));
    } catch (...) { // NOLINT(bugprone-empty-catch)
        // Best effort: nowhere is left to report a failure to log.
    }
}

template <typename... Args>
void log_error(std::format_string<Args...> fmt, Args&&... args) noexcept {
    log_message(Log::ERR, fmt, std::forward<Args>(args)...);
}

inline void notify_failure(std::string_view message) noexcept {
    try {
        HyprlandAPI::addNotification(plugin_handle,
                                     std::format("[hyprcast] {}", message),
                                     CHyprColor {1.0f, 0.2f, 0.2f, 1.0f},
                                     8000);
    } catch (...) { // NOLINT(bugprone-empty-catch)
        // Best effort: nowhere is left to report a failure to log.
    }
}

} // namespace hyprcast
