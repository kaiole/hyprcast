#pragma once

#include "hyprcast/log/log_sink.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace hyprcast::test_support {

// A log sink that keeps everything reported to it for the test to inspect.
class recording_log final : public log_sink {
public:
    struct entry {
        log_level level = log_level::trace;
        std::string message;
    };

    [[nodiscard]] const std::vector<entry>& entries() const noexcept {
        return entries_;
    }

    // Messages passed to notify_failure(), oldest first.
    [[nodiscard]] const std::vector<std::string>& failures() const noexcept {
        return failures_;
    }

    // Whether a message logged at `level` contains `text`.
    [[nodiscard]] bool contains(log_level level, std::string_view text) const {
        return std::ranges::any_of(entries_, [level, text](const entry& item) {
            return item.level == level && item.message.contains(text);
        });
    }

private:
    void do_write(log_level level, std::string_view message) override {
        entries_.push_back({.level = level, .message = std::string {message}});
    }

    void do_notify_failure(std::string_view message) override {
        failures_.emplace_back(message);
    }

    std::vector<entry> entries_;
    std::vector<std::string> failures_;
};

} // namespace hyprcast::test_support
