pkg_check_modules(HYPRCAST_SERVER_WAYLAND REQUIRED IMPORTED_TARGET GLOBAL
                  wayland-server)

add_library(hyprcast_server STATIC src/event_server.cpp src/file_lock.cpp
                                   src/wl_event_source.cpp)
add_library(hyprcast::server ALIAS hyprcast_server)

target_sources(
  hyprcast_server
  PUBLIC FILE_SET HEADERS BASE_DIRS include FILES
         include/hyprcast/server/event_server.hpp
         include/hyprcast/server/file_lock.hpp
         include/hyprcast/server/sock_file.hpp
         include/hyprcast/server/wl_event_source.hpp)
target_link_libraries(
  hyprcast_server
  PUBLIC hyprcast::common hyprcast::log
  PRIVATE hyprcast::protocol PkgConfig::HYPRCAST_SERVER_WAYLAND
          hyprcast::warnings)

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  add_subdirectory(tests)
endif()
