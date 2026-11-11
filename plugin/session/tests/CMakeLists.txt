add_executable(session_tests session_test.cpp)
target_link_libraries(
  session_tests
  PRIVATE hyprcast::session hyprcast::wire hyprcast::test_support
          hyprcast::warnings GTest::gtest_main)
gtest_discover_tests(session_tests DISCOVERY_MODE PRE_TEST)
