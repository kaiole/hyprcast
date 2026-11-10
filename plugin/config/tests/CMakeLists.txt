add_executable(config_tests config_test.cpp)
target_link_libraries(config_tests PRIVATE hyprcast::config hyprcast::warnings)
add_test(NAME configuration COMMAND config_tests)
