# Nothing here loads the plugin or needs a compositor: these tests pin what
# the adapter relies on in hyprutils, against the hyprutils that is installed.
pkg_check_modules(HYPRCAST_HYPRUTILS REQUIRED IMPORTED_TARGET hyprutils)

add_executable(adapter_contract_tests signal_contract_test.cpp)
target_link_libraries(
  adapter_contract_tests PRIVATE PkgConfig::HYPRCAST_HYPRUTILS
                                 hyprcast::warnings GTest::gtest_main)
gtest_discover_tests(adapter_contract_tests DISCOVERY_MODE PRE_TEST)
