#pragma once

#include "hyprcast/core/KeyboardId.hpp"

namespace Hyprcast {
    struct SRepeatInfo {
        KeyboardId keyboardId;
        int rate;
        int delay;
    };
}
