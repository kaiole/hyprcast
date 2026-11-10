# The only code that includes Hyprland headers: it adapts the compositor to the
# libraries next to it and is the module Hyprland loads.
pkg_check_modules(
  HYPRLAND_DEPS
  REQUIRED
  IMPORTED_TARGET
  hyprland
  libdrm
  libinput
  libudev
  pixman-1
  wayland-server
  xkbcommon)

add_library(hyprcast SHARED src/keycaster.cpp src/plugin.cpp
                            src/plugin_config.cpp src/keyboard_registry.cpp)
# The module stays where it was before this directory existed:
# <build>/plugin/libhyprcast.so.
set_target_properties(hyprcast PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                          "${CMAKE_CURRENT_BINARY_DIR}/..")

set(HYPRLAND_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../Hyprland"
    CACHE PATH "Path to the Hyprland source tree used for development")

if(EXISTS "${HYPRLAND_SOURCE_DIR}/src/version.h")
  # Plugin headers use paths such as <hyprland/src/...>. Mirror the installed
  # include layout so those paths resolve to this checkout before /usr/include.
  set(HYPRLAND_SOURCE_INCLUDE_ROOT
      "${CMAKE_CURRENT_BINARY_DIR}/hyprland-source-include")
  file(MAKE_DIRECTORY "${HYPRLAND_SOURCE_INCLUDE_ROOT}")
  file(REMOVE "${HYPRLAND_SOURCE_INCLUDE_ROOT}/hyprland")
  file(CREATE_LINK "${HYPRLAND_SOURCE_DIR}"
       "${HYPRLAND_SOURCE_INCLUDE_ROOT}/hyprland" SYMBOLIC)
  target_include_directories(hyprcast SYSTEM BEFORE
                             PRIVATE "${HYPRLAND_SOURCE_INCLUDE_ROOT}")
endif()

target_link_libraries(
  hyprcast
  PRIVATE rt
          PkgConfig::HYPRLAND_DEPS
          hyprcast::config
          hyprcast::session
          hyprcast::server
          hyprcast::log
          hyprcast::common
          hyprcast::protocol
          hyprcast::warnings)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Hyprland's smart pointers return nullptr from operator-> when empty. Once
  # that is inlined, GCC reports each use of a Hyprland global or keyboard it
  # cannot prove non-null, including from inside libstdc++. A source property
  # because it has to follow the -Wnull-dereference from hyprcast::warnings.
  set_source_files_properties(
    src/keycaster.cpp src/plugin_config.cpp src/keyboard_registry.cpp
    PROPERTIES COMPILE_OPTIONS -Wno-null-dereference)
endif()

install(TARGETS hyprcast LIBRARY DESTINATION lib)

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  add_subdirectory(tests)
endif()
