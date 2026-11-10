# Helpers shared by the tests of the plugin's libraries. Only built with the
# tests and never linked into the plugin.
add_library(hyprcast_test_support INTERFACE)
add_library(hyprcast::test_support ALIAS hyprcast_test_support)

target_sources(
  hyprcast_test_support
  INTERFACE FILE_SET HEADERS BASE_DIRS include FILES
            include/hyprcast/test_support/recording_log.hpp)
target_link_libraries(hyprcast_test_support INTERFACE hyprcast::log)
