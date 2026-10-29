add_library(hyprcast_common INTERFACE)
add_library(hyprcast::common ALIAS hyprcast_common)

target_include_directories(hyprcast_common
                           INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
