#pragma once

#include "KeyboardId.hpp"

namespace Hyprcast {
struct SRepeatInfo {
  KeyboardId keyboardId;
  int rate;
  int delay;
};
} // namespace Hyprcast
