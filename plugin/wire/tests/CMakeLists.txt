add_executable(wire_tests wire_test.cpp)
target_link_libraries(wire_tests PRIVATE hyprcast::wire libjson::libjson
                                         hyprcast::warnings GTest::gtest_main)
gtest_discover_tests(wire_tests DISCOVERY_MODE PRE_TEST)
