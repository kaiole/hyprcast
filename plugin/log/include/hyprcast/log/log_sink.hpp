#pragma once

#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

namespace hyprcast {

enum class log_level : std::uint8_t {
    trace,
    warning,
    error,
};

// Where the plugin's libraries report to. The Hyprland adapter implements it
// with the compositor's logger and notifications; tests record the calls.
// Reporting is best effort and never throws, so every error path may use it.
class log_sink {
public:
    virtual ~log_sink() = default;

    void write(log_level level, std::string_view message) noexcept {
        try {
            do_write(level, message);
        } catch (...) { // NOLINT(bugprone-empty-catch)
            // Best effort: nowhere is left to report a failure to log.
        }
    }

    // A failure the user has to act on, shown to them rather than only logged.
    void notify_failure(std::string_view message) noexcept {
        try {
            do_notify_failure(message);
        } catch (...) { // NOLINT(bugprone-empty-catch)
            // Best effort: nowhere is left to report a failure to notify.
        }
    }

private:
    virtual void do_write(log_level level, std::string_view message) = 0;
    virtual void do_notify_failure(std::string_view message) = 0;
};

template <typename... Args>
void log_message(log_sink& log,
                 log_level level,
                 std::format_string<Args...> fmt,
                 Args&&... args) noexcept {
    try {
        log.write(level, std::format(fmt, std::forward<Args>(args)...));
    } catch (...) { // NOLINT(bugprone-empty-catch)
        // Best effort: nowhere is left to report a failure to format.
    }
}

template <typename... Args>
void log_error(log_sink& log,
               std::format_string<Args...> fmt,
               Args&&... args) noexcept {
    log_message(log, log_level::error, fmt, std::forward<Args>(args)...);
}

} // namespace hyprcast
