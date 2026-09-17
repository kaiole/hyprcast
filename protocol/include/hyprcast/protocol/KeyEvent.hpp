#pragma once

#include "KeyState.hpp"
#include "KeyboardId.hpp"

namespace Hyprcast {
struct SKeyEvent {
  KeyboardId keyboardId;
  uint32_t timeMs;
  uint32_t keycode;
  eKeyState state;
};
} // namespace Hyprcast
