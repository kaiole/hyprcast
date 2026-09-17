add_library(hyprcast_protocol INTERFACE)
add_library(hyprcast::protocol ALIAS hyprcast_protocol)

target_include_directories(
  hyprcast_protocol INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
