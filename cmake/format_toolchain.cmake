cmake_minimum_required(VERSION 3.20)

function(mpf_require_clang_format_version version_output)
  if(NOT version_output MATCHES "clang-format version ([0-9]+)\\.")
    message(FATAL_ERROR "cannot identify clang-format version: ${version_output}")
  endif()
  if(NOT CMAKE_MATCH_1 STREQUAL "18")
    message(FATAL_ERROR
      "MPF formatting requires clang-format 18; found: ${version_output}. "
      "Set MPF_CLANG_FORMAT_EXECUTABLE to the clang-format 18 executable.")
  endif()
endfunction()
