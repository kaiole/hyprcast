add_library(hyprcast_common INTERFACE)
add_library(hyprcast::common ALIAS hyprcast_common)

configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/hyprcast/version.hpp" @ONLY)

target_sources(
  hyprcast_common
  INTERFACE FILE_SET
            HEADERS
            BASE_DIRS
            include
            "${CMAKE_CURRENT_BINARY_DIR}/generated"
            FILES
            include/hyprcast/os/unique_fd.hpp
            "${CMAKE_CURRENT_BINARY_DIR}/generated/hyprcast/version.hpp")
