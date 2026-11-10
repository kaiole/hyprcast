add_library(hyprcast_wire STATIC src/messages.cpp)
add_library(hyprcast::wire ALIAS hyprcast_wire)

target_sources(
  hyprcast_wire PUBLIC FILE_SET HEADERS BASE_DIRS include FILES
                       include/hyprcast/wire/messages.hpp)
target_link_libraries(hyprcast_wire PUBLIC hyprcast::protocol
                      PRIVATE libjson::libjson hyprcast::warnings)
