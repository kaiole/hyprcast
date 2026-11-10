cmake_minimum_required(VERSION 3.27...4.4)
project(
  hyprcast
  VERSION 0.1.0
  DESCRIPTION "A keycaster for Hyprland"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/warnings.cmake)

if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
  if(BUILD_TESTING)
    include(cmake/gtest.cmake)
  endif()
endif()

option(HYPRCAST_BUILD_PLUGIN "Build the Hyprland plugin" ON)
option(HYPRCAST_BUILD_OVERLAY "Build the Qt Quick overlay" ON)

add_subdirectory(common)
add_subdirectory(protocol)
if(HYPRCAST_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()
if(HYPRCAST_BUILD_OVERLAY)
  add_subdirectory(overlay)
endif()
