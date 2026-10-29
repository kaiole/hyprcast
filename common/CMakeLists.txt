add_library(hyprcast_common INTERFACE)
add_library(hyprcast::common ALIAS hyprcast_common)

configure_file(Version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/hyprcast/Version.hpp" @ONLY)

target_include_directories(
  hyprcast_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
                            "${CMAKE_CURRENT_BINARY_DIR}/generated")
