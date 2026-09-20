#pragma once

#include <cstdint>

namespace Hyprcast {
    struct SModifiers {
        uint32_t depressed, latched, locked, group;
    };
}
