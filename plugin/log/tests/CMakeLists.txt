add_executable(log_tests log_test.cpp)
target_link_libraries(log_tests PRIVATE hyprcast::log hyprcast::test_support
                                        hyprcast::warnings GTest::gtest_main)
gtest_discover_tests(log_tests DISCOVERY_MODE PRE_TEST)
