#pragma once

#include "hyprcast/version.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>

#include <string_view>

namespace hyprcast {

// Set once by PLUGIN_INIT; every HyprlandAPI call needs it.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline HANDLE plugin_handle = nullptr;

inline constexpr std::string_view plugin_name = "Hyprcast";
inline constexpr std::string_view description = "Keycaster for Hyprland";
inline constexpr std::string_view author = "kaiole";
inline constexpr std::string_view version = project_version;

} // namespace hyprcast
