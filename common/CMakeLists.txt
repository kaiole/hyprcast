add_library(hyprcast_common INTERFACE)
add_library(hyprcast::common ALIAS hyprcast_common)

configure_file(Version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/hyprcast/Version.hpp" @ONLY)

target_sources(
  hyprcast_common
  INTERFACE FILE_SET
            HEADERS
            BASE_DIRS
            include
            "${CMAKE_CURRENT_BINARY_DIR}/generated"
            FILES
            include/hyprcast/os/UniqueFd.hpp
            "${CMAKE_CURRENT_BINARY_DIR}/generated/hyprcast/Version.hpp")
