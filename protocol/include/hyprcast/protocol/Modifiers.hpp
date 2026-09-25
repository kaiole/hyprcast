#pragma once

#include <cstdint>

namespace Hyprcast {
    struct SModifiers {
        std::uint32_t depressed, latched, locked, group;
    };
}
