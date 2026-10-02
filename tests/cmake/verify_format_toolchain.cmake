cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/format_toolchain.cmake")
if(DEFINED TEST_VERSION)
  mpf_require_clang_format_version("${TEST_VERSION}")
  return()
endif()

foreach(version IN ITEMS
    "clang-format version 18.1.3"
    "Homebrew clang-format version 18.1.8"
    "Ubuntu clang-format version 18.0.0 (1ubuntu1)")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DTEST_VERSION=${version}"
      -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "supported clang-format version was rejected: ${version}\n${output}${error}")
  endif()
endforeach()

foreach(version IN ITEMS
    "clang-format version 17.0.6"
    "clang-format version 19.1.8"
    "Homebrew clang-format version 22.1.4"
    "cmake version 3.31.0"
    "clang-format version 18broken")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DTEST_VERSION=${version}"
      -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(result EQUAL 0 OR NOT "${output}${error}" MATCHES
      "requires clang-format 18|cannot identify clang-format version")
    message(FATAL_ERROR "invalid formatter version did not fail closed: ${version}\n${output}${error}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.."
    "-DCLANG_FORMAT=${CMAKE_COMMAND}"
    -DMODE=check
    -P "${CMAKE_CURRENT_LIST_DIR}/../../cmake/verify_format.cmake"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "cannot identify clang-format version")
  message(FATAL_ERROR "format verification accepted a non-formatter executable\n${output}${error}")
endif()

message(STATUS "clang-format toolchain contract passed")
