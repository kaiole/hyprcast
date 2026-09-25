find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(LayerShellQt REQUIRED)

add_executable(hyprcast-overlay src/main.cpp)
qt_add_resources(
  hyprcast-overlay overlay_qml
  PREFIX "/hyprcast/overlay"
  FILES qml/Proof.qml)

target_link_libraries(
  hyprcast-overlay PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
                           LayerShellQt::Interface)

install(TARGETS hyprcast-overlay RUNTIME DESTINATION bin)
