# Match Hyprland's supported pkg-config names, then check both version bounds.
pkg_search_module(
  HYPRCAST_LUA
  REQUIRED
  IMPORTED_TARGET
  GLOBAL
  lua55
  lua5.5
  lua-55
  lua-5.5
  lua)
if(HYPRCAST_LUA_VERSION VERSION_LESS "5.5" OR NOT HYPRCAST_LUA_VERSION
                                              VERSION_LESS "5.6")
  message(
    FATAL_ERROR
      "Lua >= 5.5 and < 5.6 is required; found ${HYPRCAST_LUA_VERSION}")
endif()

add_library(hyprcast_config STATIC src/config.cpp)
add_library(hyprcast::config ALIAS hyprcast_config)

target_sources(
  hyprcast_config PUBLIC FILE_SET HEADERS BASE_DIRS include FILES
                         include/hyprcast/config/config.hpp)
target_link_libraries(hyprcast_config PUBLIC PkgConfig::HYPRCAST_LUA
                      PRIVATE hyprcast::warnings)

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  add_subdirectory(tests)
endif()
