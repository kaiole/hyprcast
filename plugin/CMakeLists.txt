# Everything in this directory is linked into the plugin, a shared object.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(HYPRCAST_PLUGIN_SOURCES
    src/keycaster.cpp
    src/hyprland/plugin.cpp
    src/hyprland/plugin_config.cpp
    src/hyprland/keyboard_registry.cpp
    src/ipc/event_server.cpp
    src/os/file_lock.cpp
    src/wayland/wl_event_source.cpp)

include(FetchContent)

# libjson has no releases and no package to find; the pin is a commit archive.
# Update URL and hash together.
FetchContent_Declare(
  libjson
  URL https://github.com/kaiole/libjson/archive/013397b90735d630d89eab79c2d097bf2ec9876d.tar.gz
  URL_HASH
    SHA256=f2c86456ba8d0ea0b5ad94ae2c562f63532116455a208d5616149ad27be3ea36
  SYSTEM)
block()
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(CMAKE_CXX_CLANG_TIDY "")
# libjson registers its own tests with ctest when BUILD_TESTING is on.
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(libjson)
endblock()

find_package(PkgConfig REQUIRED)

add_subdirectory(log)
add_subdirectory(config)
add_subdirectory(wire)

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
  target_include_directories(hyprcast SYSTEM BEFORE
                             PRIVATE "${HYPRLAND_SOURCE_INCLUDE_ROOT}")
endif()

target_include_directories(hyprcast PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
  hyprcast PRIVATE rt PkgConfig::HYPRLAND_DEPS hyprcast::config
                   hyprcast::wire hyprcast::log hyprcast::common
                   hyprcast::protocol hyprcast::warnings)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Hyprland's smart pointers return nullptr from operator-> when empty. Once
  # that is inlined, GCC reports each use of a Hyprland global or keyboard it
  # cannot prove non-null, including from inside libstdc++. A source property
  # because it has to follow the -Wnull-dereference from hyprcast::warnings.
  set_source_files_properties(
    src/hyprland/plugin_config.cpp src/hyprland/keyboard_registry.cpp
    src/ipc/event_server.cpp PROPERTIES COMPILE_OPTIONS -Wno-null-dereference)
endif()

install(TARGETS hyprcast LIBRARY DESTINATION lib)
