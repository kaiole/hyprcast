execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen "${OVERLAY}" --help
  RESULT_VARIABLE result OUTPUT_VARIABLE help ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR help MATCHES "show-held-keys|hide-held-keys")
  message(FATAL_ERROR "obsolete held-key flags still appear in CLI help: ${result} ${help} ${error}")
endif()

foreach(flag show-held-keys hide-held-keys)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen "${OVERLAY}" --${flag}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(result EQUAL 0 OR NOT error MATCHES "Unknown option '${flag}'")
    message(FATAL_ERROR "--${flag} was not rejected as an unknown option: ${result} ${output} ${error}")
  endif()
endforeach()
