#pragma once

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include "hyprland/Plugin.hpp"

#include <format>
#include <string_view>
#include <utility>

namespace Hyprcast {
    template <typename... Args>
    void logMessage(Hyprutils::CLI::eLogLevel level, std::format_string<Args...> fmt, Args&&... args) noexcept {
        try {
            Log::logger->log(level, "[hyprcast] {}", std::format(fmt, std::forward<Args>(args)...));
        } catch (...) {
            // Best-effort logging.
        }
    }

    template <typename... Args>
    void logError(std::format_string<Args...> fmt, Args&&... args) noexcept {
        logMessage(Log::ERR, fmt, std::forward<Args>(args)...);
    }

    inline void notifyFailure(std::string_view message) noexcept {
        try {
            HyprlandAPI::addNotification(PHANDLE, std::format("[hyprcast] {}", message), CHyprColor{1.0F, 0.2F, 0.2F, 1.0F}, 8000);
        } catch (...) {
            // Best-effort logging.
        }
    }
}
