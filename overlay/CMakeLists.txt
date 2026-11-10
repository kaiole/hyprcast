find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick)
find_package(LayerShellQt REQUIRED)
find_package(PkgConfig REQUIRED)

# The system toml++ when present, else the pinned release. Update URL and hash
# together.
include(FetchContent)

FetchContent_Declare(
  tomlplusplus
  URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
  URL_HASH
    SHA256=8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155
  SYSTEM FIND_PACKAGE_ARGS 3.4 CONFIG)
block()
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(CMAKE_CXX_CLANG_TIDY "")
FetchContent_MakeAvailable(tomlplusplus)
endblock()

pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET "xkbcommon>=0.7")

add_library(
  hyprcast_overlay_ipc STATIC
  src/ipc/Restart.cpp
  src/ipc/Restart.hpp
  src/ipc/CaptureController.cpp
  src/ipc/CaptureController.hpp
  src/ipc/OverlayInstance.cpp
  src/ipc/OverlayInstance.hpp
  src/ipc/ConnectionState.cpp
  src/ipc/IpcClient.cpp
  src/ipc/IpcClient.hpp
  src/ipc/LineFramer.cpp
  src/ipc/Protocol.cpp)
set_target_properties(hyprcast_overlay_ipc PROPERTIES AUTOMOC ON)
target_include_directories(hyprcast_overlay_ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hyprcast_overlay_ipc PRIVATE hyprcast::warnings PUBLIC hyprcast::common hyprcast::protocol Qt6::Core Qt6::Network)

add_library(
  hyprcast_overlay_history STATIC
  src/history/InputHistory.cpp
  src/history/InputHistory.hpp
  src/history/InterpretedAction.hpp)
set_target_properties(hyprcast_overlay_history PROPERTIES AUTOMOC ON)
target_include_directories(hyprcast_overlay_history PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hyprcast_overlay_history PRIVATE hyprcast::warnings PUBLIC Qt6::Core)

add_library(
  hyprcast_overlay_input STATIC
  src/input/KeyboardInterpreter.cpp
  src/input/KeyboardInterpreter.hpp
  src/input/KeyboardPresenter.cpp
  src/input/KeyboardPresenter.hpp)
set_target_properties(hyprcast_overlay_input PROPERTIES AUTOMOC ON)
target_include_directories(hyprcast_overlay_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hyprcast_overlay_input PRIVATE hyprcast::warnings PUBLIC Qt6::Core hyprcast::protocol hyprcast_overlay_history PkgConfig::XKBCOMMON)

add_library(hyprcast_overlay_config STATIC src/config/OverlayConfig.cpp src/config/OverlayConfig.hpp src/config/TomlReader.cpp src/config/TomlReader.hpp)
set_target_properties(hyprcast_overlay_config PROPERTIES AUTOMOC ON)
target_include_directories(hyprcast_overlay_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hyprcast_overlay_config PRIVATE hyprcast::warnings PUBLIC Qt6::Core Qt6::Gui tomlplusplus::tomlplusplus)

add_library(hyprcast_overlay_theme STATIC src/theme/Theme.cpp src/theme/Theme.hpp)
set_target_properties(hyprcast_overlay_theme PROPERTIES AUTOMOC ON)
target_include_directories(hyprcast_overlay_theme PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hyprcast_overlay_theme PRIVATE hyprcast::warnings PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick hyprcast_overlay_config hyprcast_overlay_input tomlplusplus::tomlplusplus)

add_executable(hyprcast-overlay src/main.cpp)
qt_add_resources(
  hyprcast-overlay overlay_qml
  PREFIX "/hyprcast/overlay"
  FILES qml/ThemeHost.qml qml/TextPresentation.qml
        qml/themes/default/theme.toml qml/themes/default/Main.qml)

target_link_libraries(
  hyprcast-overlay PRIVATE hyprcast::warnings hyprcast_overlay_ipc hyprcast_overlay_input hyprcast_overlay_config hyprcast_overlay_theme Qt6::Core
                           Qt6::Gui Qt6::Qml Qt6::Quick LayerShellQt::Interface)

install(DIRECTORY examples/themes/ DESTINATION share/hyprcast/themes)

if(BUILD_TESTING)
  add_executable(overlay_restart_tests tests/Restart.cpp)
  target_link_libraries(overlay_restart_tests PRIVATE hyprcast::warnings hyprcast_overlay_ipc PkgConfig::XKBCOMMON)
  add_dependencies(overlay_restart_tests hyprcast-overlay)
  add_test(NAME overlay-restart COMMAND overlay_restart_tests $<TARGET_FILE:hyprcast-overlay>)
  set_tests_properties(overlay-restart PROPERTIES TIMEOUT 90)

  add_executable(overlay_lifecycle_tests tests/Lifecycle.cpp)
  target_link_libraries(overlay_lifecycle_tests PRIVATE hyprcast::warnings hyprcast_overlay_ipc)
  add_test(NAME overlay-lifecycle COMMAND overlay_lifecycle_tests)

  add_executable(overlay_ipc_tests tests/Ipc.cpp)
  target_link_libraries(overlay_ipc_tests PRIVATE hyprcast::warnings hyprcast_overlay_ipc Qt6::Network)
  add_test(NAME overlay-ipc COMMAND overlay_ipc_tests)

  add_executable(overlay_input_tests tests/Input.cpp)
  target_link_libraries(overlay_input_tests PRIVATE hyprcast::warnings hyprcast_overlay_input)
  add_test(NAME overlay-input COMMAND overlay_input_tests)

  add_executable(overlay_history_tests tests/History.cpp)
  target_link_libraries(overlay_history_tests PRIVATE hyprcast::warnings hyprcast_overlay_history)
  add_test(NAME overlay-history COMMAND overlay_history_tests)

  add_executable(overlay_config_tests tests/Config.cpp)
  target_link_libraries(overlay_config_tests PRIVATE hyprcast::warnings hyprcast_overlay_config)
  add_test(NAME overlay-config COMMAND overlay_config_tests)

  add_executable(overlay_theme_tests tests/Theme.cpp)
  target_compile_definitions(overlay_theme_tests PRIVATE HYPRCAST_OVERLAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(overlay_theme_tests PRIVATE hyprcast::warnings hyprcast_overlay_theme)
  add_test(NAME overlay-theme COMMAND overlay_theme_tests)
  set_tests_properties(overlay-theme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Feeds what the plugin encodes to the overlay's parser, so it needs both
  # components. GoogleTest is only set up for a top-level build.
  if(PROJECT_IS_TOP_LEVEL AND TARGET hyprcast::wire)
    add_executable(wire_contract_tests tests/WireContract.cpp)
    target_link_libraries(wire_contract_tests PRIVATE hyprcast::warnings hyprcast::wire hyprcast_overlay_ipc GTest::gtest_main)
    gtest_discover_tests(wire_contract_tests DISCOVERY_MODE PRE_TEST)
  endif()
endif()

install(TARGETS hyprcast-overlay RUNTIME DESTINATION bin)
