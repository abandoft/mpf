cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROJECT_BUILD_DIR OR NOT DEFINED BUILD_DIR OR NOT DEFINED SOURCE_DIR OR
   NOT DEFINED STAGE OR NOT DEFINED LICENSE_FILE OR NOT DEFINED INSTALL_DOCDIR OR
   NOT DEFINED CONFIG OR NOT DEFINED PROJECT_VERSION)
  message(FATAL_ERROR
    "PROJECT_BUILD_DIR, BUILD_DIR, SOURCE_DIR, STAGE, LICENSE_FILE, INSTALL_DOCDIR, CONFIG and PROJECT_VERSION are required")
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

include("${CMAKE_CURRENT_LIST_DIR}/verify_staged_package.cmake")
