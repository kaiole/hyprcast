cmake_minimum_required(VERSION 3.27)

project(
  hyprcast
  VERSION 0.1.0
  DESCRIPTION "A keycaster plugin for Hyprland"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(HYPRCAST_PLUGIN_SOURCES
    src/hyprland/Hyprcast.cpp src/hyprland/InputCapture.cpp
    src/hyprland/Plugin.cpp)

find_package(PkgConfig REQUIRED)
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

add_library(hyprcast SHARED ${HYPRCAST_PLUGIN_SOURCES})
set(HYPRLAND_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/Hyprland"
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
  target_include_directories(hyprcast BEFORE
                             PRIVATE "${HYPRLAND_SOURCE_INCLUDE_ROOT}")
endif()

target_include_directories(hyprcast
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hyprcast PRIVATE rt PkgConfig::HYPRLAND_DEPS)

install(TARGETS hyprcast LIBRARY DESTINATION lib)
