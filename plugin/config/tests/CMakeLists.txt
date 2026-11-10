add_executable(config_tests config_test.cpp)
target_link_libraries(config_tests PRIVATE hyprcast::config hyprcast::warnings
                                           GTest::gtest_main)
gtest_discover_tests(config_tests DISCOVERY_MODE PRE_TEST)
