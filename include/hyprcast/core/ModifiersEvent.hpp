#pragma once

#include "KeyboardId.hpp"

#include <cstdint>

namespace Hyprcast {
    struct SModifiersEvent {
        KeyboardId keyboardId;
        uint32_t   depressed;
        uint32_t   latched;
        uint32_t   locked;
        uint32_t   group;
    };
};
