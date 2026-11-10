add_library(hyprcast_log INTERFACE)
add_library(hyprcast::log ALIAS hyprcast_log)

target_sources(
  hyprcast_log INTERFACE FILE_SET HEADERS BASE_DIRS include FILES
                         include/hyprcast/log/log_sink.hpp
                         include/hyprcast/log/run_guarded.hpp)

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  add_subdirectory(tests)
endif()
