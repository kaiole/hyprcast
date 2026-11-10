#pragma once

#include "hyprcast/Version.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>

#include <string_view>

namespace Hyprcast {

// Set once by PLUGIN_INIT; every HyprlandAPI call needs it.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline HANDLE PHANDLE = nullptr;

inline constexpr std::string_view PLUGIN_NAME = "Hyprcast";
inline constexpr std::string_view DESCRIPTION = "Keycaster for Hyprland";
inline constexpr std::string_view AUTHOR = "kaiole";
inline constexpr std::string_view VERSION = PROJECT_VERSION;

} // namespace Hyprcast
