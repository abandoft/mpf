cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED CLANG_FORMAT OR NOT DEFINED MODE)
  message(FATAL_ERROR "verify_format.cmake requires SOURCE_DIR, CLANG_FORMAT and MODE")
endif()
if(NOT EXISTS "${CLANG_FORMAT}")
  message(FATAL_ERROR "clang-format executable was not found: ${CLANG_FORMAT}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/format_toolchain.cmake")
execute_process(
  COMMAND "${CLANG_FORMAT}" --version
  RESULT_VARIABLE version_result
  OUTPUT_VARIABLE version_output
  ERROR_VARIABLE version_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT version_result EQUAL 0)
  message(FATAL_ERROR "cannot run clang-format --version: ${version_error}")
endif()
mpf_require_clang_format_version("${version_output}")

file(GLOB_RECURSE format_files LIST_DIRECTORIES FALSE
  "${SOURCE_DIR}/include/*.hpp"
  "${SOURCE_DIR}/src/*.cpp"
  "${SOURCE_DIR}/src/*.hpp"
  "${SOURCE_DIR}/tests/*.cpp"
  "${SOURCE_DIR}/tests/*.hpp"
  "${SOURCE_DIR}/examples/embedding/*.cpp")
list(SORT format_files)
if(format_files STREQUAL "")
  message(FATAL_ERROR "no C++ source files were found for formatting")
endif()

if(MODE STREQUAL "check")
  set(format_arguments --dry-run --Werror)
elseif(MODE STREQUAL "write")
  set(format_arguments -i)
else()
  message(FATAL_ERROR "unknown format mode: ${MODE}")
endif()

execute_process(
  COMMAND "${CLANG_FORMAT}" ${format_arguments} ${format_files}
  RESULT_VARIABLE format_result
  OUTPUT_VARIABLE format_output
  ERROR_VARIABLE format_error)
if(NOT format_result EQUAL 0)
  message(FATAL_ERROR
    "clang-format ${MODE} failed:\n${format_output}\n${format_error}")
endif()
