#pragma once

#include "log.hpp"

#include <exception>
#include <format>
#include <string_view>
#include <type_traits>
#include <utility>

namespace hyprcast {

template <typename Function, typename Recovery>
void run_guarded(std::string_view context,
                 Function&& function,
                 Recovery&& recover) {
    static_assert(std::is_nothrow_invocable_v<Recovery&&>);

    try {
        std::forward<Function>(function)();
        return;
    } catch (const std::exception& error) {
        log_error("{}: {}", context, error.what());
    } catch (...) {
        log_error("{}: unknown exception", context);
    }

    std::forward<Recovery>(recover)();
}

} // namespace hyprcast
