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

file(GLOB_RECURSE HYPRCAST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

if(NOT HYPRCAST_SOURCES)
  message(STATUS "hyprcast has no source files yet; skipping the plugin target")
  return()
endif()

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

add_library(hyprcast SHARED ${HYPRCAST_SOURCES})
target_include_directories(hyprcast
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hyprcast PRIVATE rt PkgConfig::HYPRLAND_DEPS)

install(TARGETS hyprcast LIBRARY DESTINATION lib)
