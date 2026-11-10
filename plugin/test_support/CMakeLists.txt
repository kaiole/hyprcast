# Helpers shared by the tests of the plugin's libraries. Only built with the
# tests and never linked into the plugin.
pkg_check_modules(HYPRCAST_TEST_WAYLAND REQUIRED IMPORTED_TARGET GLOBAL
                  wayland-server)

add_library(hyprcast_test_support INTERFACE)
add_library(hyprcast::test_support ALIAS hyprcast_test_support)

target_sources(
  hyprcast_test_support
  INTERFACE FILE_SET HEADERS BASE_DIRS include FILES
            include/hyprcast/test_support/event_loop.hpp
            include/hyprcast/test_support/recording_log.hpp
            include/hyprcast/test_support/socket_client.hpp
            include/hyprcast/test_support/temp_dir.hpp)
target_link_libraries(
  hyprcast_test_support INTERFACE hyprcast::common hyprcast::log
                                  PkgConfig::HYPRCAST_TEST_WAYLAND)
