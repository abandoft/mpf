if(NOT DEFINED PROJECT_BUILD_DIR OR NOT DEFINED BUILD_DIR OR NOT DEFINED SOURCE_DIR OR
   NOT DEFINED STAGE OR NOT DEFINED CONFIG)
  message(FATAL_ERROR "PROJECT_BUILD_DIR, BUILD_DIR, SOURCE_DIR, STAGE and CONFIG are required")
endif()

file(REMOVE_RECURSE "${STAGE}" "${BUILD_DIR}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BUILD_DIR}" --config "${CONFIG}"
    --prefix "${STAGE}"
  RESULT_VARIABLE install_status
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT install_status EQUAL 0)
  message(FATAL_ERROR "installed example staging failed:\n${install_output}\n${install_error}")
endif()

foreach(example IN ITEMS frontend backend)
  set(example_build "${BUILD_DIR}/${example}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/examples/installed/${example}"
      -B "${example_build}" -DCMAKE_BUILD_TYPE=${CONFIG} -DCMAKE_PREFIX_PATH=${STAGE}
    RESULT_VARIABLE configure_status
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
  if(NOT configure_status EQUAL 0)
    message(FATAL_ERROR
      "installed ${example} example configure failed:\n${configure_output}\n${configure_error}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${example_build}" --config "${CONFIG}" --parallel
    RESULT_VARIABLE build_status
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
  if(NOT build_status EQUAL 0)
    message(FATAL_ERROR
      "installed ${example} example build failed:\n${build_output}\n${build_error}")
  endif()
  if(WIN32)
    set(executable "${example_build}/${CONFIG}/mpf-installed-${example}-example.exe")
  else()
    set(executable "${example_build}/mpf-installed-${example}-example")
  endif()
  execute_process(COMMAND "${executable}" RESULT_VARIABLE run_status)
  if(NOT run_status EQUAL 0)
    message(FATAL_ERROR "installed ${example} example failed (${run_status})")
  endif()
endforeach()
