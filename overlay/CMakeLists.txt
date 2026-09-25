find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick)
find_package(LayerShellQt REQUIRED)

add_library(
  hyprcast_overlay_ipc STATIC
  src/ipc/ConnectionState.cpp
  src/ipc/IpcClient.cpp
  src/ipc/IpcClient.hpp
  src/ipc/LineFramer.cpp
  src/ipc/Protocol.cpp)
set_target_properties(hyprcast_overlay_ipc PROPERTIES AUTOMOC ON)
target_include_directories(hyprcast_overlay_ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hyprcast_overlay_ipc PUBLIC Qt6::Core Qt6::Network)

add_executable(hyprcast-overlay src/main.cpp)
qt_add_resources(
  hyprcast-overlay overlay_qml
  PREFIX "/hyprcast/overlay"
  FILES qml/Proof.qml)

target_link_libraries(
  hyprcast-overlay PRIVATE hyprcast_overlay_ipc Qt6::Core Qt6::Gui Qt6::Qml
                           Qt6::Quick LayerShellQt::Interface)

if(BUILD_TESTING)
  add_executable(overlay_ipc_tests tests/Ipc.cpp)
  target_link_libraries(overlay_ipc_tests PRIVATE hyprcast_overlay_ipc Qt6::Network)
  add_test(NAME overlay-ipc COMMAND overlay_ipc_tests)
endif()

install(TARGETS hyprcast-overlay RUNTIME DESTINATION bin)
