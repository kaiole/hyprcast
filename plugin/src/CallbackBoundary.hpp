#pragma once

#include <exception>
#include <format>
#include "Log.hpp"
#include <string_view>
#include <type_traits>
#include <utility>

namespace Hyprcast {
    template <typename Function, typename Recovery>
    void runGuarded(std::string_view context, Function&& function, Recovery&& recover) {
        static_assert(std::is_nothrow_invocable_v<Recovery&&>);

        try {
            std::forward<Function>(function)();
            return;
        } catch (const std::exception& error) { logError("{}: {}", context, error.what()); } catch (...) {
            logError("{}: unknown exception", context);
        }

        std::forward<Recovery>(recover)();
    }
}
