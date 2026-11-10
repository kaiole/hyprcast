add_executable(server_tests server_test.cpp)
target_link_libraries(
  server_tests PRIVATE hyprcast::server hyprcast::test_support
                       hyprcast::warnings GTest::gtest_main)
gtest_discover_tests(server_tests DISCOVERY_MODE PRE_TEST)
