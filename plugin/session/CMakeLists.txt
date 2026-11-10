add_library(hyprcast_session STATIC src/session.cpp)
add_library(hyprcast::session ALIAS hyprcast_session)

target_sources(
  hyprcast_session PUBLIC FILE_SET HEADERS BASE_DIRS include FILES
                          include/hyprcast/session/session.hpp)
target_link_libraries(
  hyprcast_session
  PUBLIC hyprcast::server hyprcast::protocol hyprcast::log
  PRIVATE hyprcast::wire hyprcast::warnings)
