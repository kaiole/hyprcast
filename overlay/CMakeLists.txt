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

# GoogleTest is only set up for a top-level build.
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  # Each test executable has its own main (tests/Support.hpp): it needs a Qt
  # application object and runs away from the live session.
  function(hyprcast_overlay_test name source)
    add_executable(${name} tests/${source} tests/Support.hpp)
    target_link_libraries(${name} PRIVATE hyprcast::warnings GTest::gtest ${ARGN})
  endfunction()

  hyprcast_overlay_test(overlay_restart_tests Restart.cpp hyprcast_overlay_ipc PkgConfig::XKBCOMMON)
  # The tests start the overlay itself.
  add_dependencies(overlay_restart_tests hyprcast-overlay)
  target_compile_definitions(overlay_restart_tests PRIVATE HYPRCAST_OVERLAY_BINARY="$<TARGET_FILE:hyprcast-overlay>")
  gtest_discover_tests(overlay_restart_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 90)

  hyprcast_overlay_test(overlay_lifecycle_tests Lifecycle.cpp hyprcast_overlay_ipc)
  gtest_discover_tests(overlay_lifecycle_tests DISCOVERY_MODE PRE_TEST)

  hyprcast_overlay_test(overlay_ipc_tests Ipc.cpp hyprcast_overlay_ipc Qt6::Network)
  gtest_discover_tests(overlay_ipc_tests DISCOVERY_MODE PRE_TEST)

  hyprcast_overlay_test(overlay_input_tests Input.cpp hyprcast_overlay_input)
  gtest_discover_tests(overlay_input_tests DISCOVERY_MODE PRE_TEST)

  hyprcast_overlay_test(overlay_history_tests History.cpp hyprcast_overlay_history)
  gtest_discover_tests(overlay_history_tests DISCOVERY_MODE PRE_TEST)

  hyprcast_overlay_test(overlay_config_tests Config.cpp hyprcast_overlay_config)
  gtest_discover_tests(overlay_config_tests DISCOVERY_MODE PRE_TEST)

  hyprcast_overlay_test(overlay_theme_tests Theme.cpp hyprcast_overlay_theme)
  target_compile_definitions(overlay_theme_tests PRIVATE HYPRCAST_OVERLAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  gtest_discover_tests(
    overlay_theme_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/lsan.supp"
  )

  # Feeds what the plugin encodes to the overlay's parser, so it needs both
  # components.
  if(TARGET hyprcast::wire)
    add_executable(wire_contract_tests tests/WireContract.cpp)
    target_link_libraries(wire_contract_tests PRIVATE hyprcast::warnings hyprcast::wire hyprcast_overlay_ipc GTest::gtest_main)
    gtest_discover_tests(wire_contract_tests DISCOVERY_MODE PRE_TEST)
  endif()
endif()

install(TARGETS hyprcast-overlay RUNTIME DESTINATION bin)
