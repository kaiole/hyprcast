#pragma once

#include "hyprcast/log/log_sink.hpp"

#include <exception>
#include <string_view>
#include <type_traits>
#include <utility>

namespace hyprcast {

// Runs `function` at a boundary no exception may cross (a compositor or event
// loop callback). An exception is logged with `context` and then `recover`
// runs.
template <typename Function, typename Recovery>
void run_guarded(log_sink& log,
                 std::string_view context,
                 Function&& function,
                 Recovery&& recover) {
    static_assert(std::is_nothrow_invocable_v<Recovery&&>);

    try {
        std::forward<Function>(function)();
        return;
    } catch (const std::exception& error) {
        log_error(log, "{}: {}", context, error.what());
    } catch (...) {
        log_error(log, "{}: unknown exception", context);
    }

    std::forward<Recovery>(recover)();
}

} // namespace hyprcast
