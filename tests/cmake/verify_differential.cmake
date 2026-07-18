cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MPFC OR NOT DEFINED INPUT OR NOT DEFINED CASE_NAME OR
   NOT DEFINED TEST_SOURCE_DIR OR NOT DEFINED TEST_BINARY_DIR OR
   NOT DEFINED EXPECTED_OUTPUT OR NOT DEFINED OUTPUT_MODE)
  message(FATAL_ERROR "verify_differential.cmake received incomplete arguments")
endif()

function(normalize_output value output_variable)
  string(REPLACE "\r\n" "\n" normalized "${value}")
  string(REPLACE "\r" "\n" normalized "${normalized}")
  if(OUTPUT_MODE STREQUAL "tokens")
    string(REGEX REPLACE "[ \t\n]+" " " normalized "${normalized}")
    string(STRIP "${normalized}" normalized)
  elseif(OUTPUT_MODE STREQUAL "lines")
    string(REGEX REPLACE "[ \t]+\n" "\n" normalized "${normalized}")
    string(REGEX REPLACE "\n+$" "" normalized "${normalized}")
  else()
    message(FATAL_ERROR "unknown differential output mode: ${OUTPUT_MODE}")
  endif()
  set(${output_variable} "${normalized}" PARENT_SCOPE)
endfunction()

function(require_success stage status stdout stderr)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR
      "${CASE_NAME}: ${stage} failed (${status})\nstdout:\n${stdout}\nstderr:\n${stderr}")
  endif()
endfunction()

function(require_warning stage stderr)
  if(NOT DEFINED EXPECTED_WARNING OR EXPECTED_WARNING STREQUAL "")
    return()
  endif()
  string(FIND "${stderr}" "${EXPECTED_WARNING}" warning_offset)
  if(warning_offset EQUAL -1)
    message(FATAL_ERROR
      "${CASE_NAME}: ${stage} did not report warning '${EXPECTED_WARNING}'\nstderr:\n${stderr}")
  endif()
endfunction()

function(record_text value output_variable)
  string(REPLACE "\\" "\\\\" recorded "${value}")
  string(REPLACE "\r" "\\r" recorded "${recorded}")
  string(REPLACE "\n" "\\n" recorded "${recorded}")
  set(${output_variable} "${recorded}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${TEST_BINARY_DIR}")
file(MAKE_DIRECTORY "${TEST_BINARY_DIR}")

set(javascript_source "${TEST_BINARY_DIR}/generated.mjs")
set(javascript_output "<not-run>")
set(javascript_run_error "<not-run>")
set(javascript_available FALSE)
if(DEFINED NODE AND NOT NODE STREQUAL "" AND NOT NODE MATCHES "-NOTFOUND$")
  execute_process(
    COMMAND "${MPFC}" --target javascript --no-banner "${INPUT}" -o "${javascript_source}"
    RESULT_VARIABLE javascript_transpile_status
    OUTPUT_VARIABLE javascript_transpile_output
    ERROR_VARIABLE javascript_transpile_error)
  require_success("JavaScript transpilation" "${javascript_transpile_status}"
                  "${javascript_transpile_output}" "${javascript_transpile_error}")
  execute_process(
    COMMAND "${NODE}" --check "${javascript_source}"
    RESULT_VARIABLE javascript_syntax_status
    OUTPUT_VARIABLE javascript_syntax_output
    ERROR_VARIABLE javascript_syntax_error)
  require_success("JavaScript syntax check" "${javascript_syntax_status}"
                  "${javascript_syntax_output}" "${javascript_syntax_error}")
  execute_process(
    COMMAND "${NODE}" "${javascript_source}"
    RESULT_VARIABLE javascript_run_status
    OUTPUT_VARIABLE javascript_run_output
    ERROR_VARIABLE javascript_run_error)
  require_success("JavaScript execution" "${javascript_run_status}"
                  "${javascript_run_output}" "${javascript_run_error}")
  require_warning("JavaScript execution" "${javascript_run_error}")
  normalize_output("${javascript_run_output}" javascript_output)
  set(javascript_available TRUE)
endif()

set(cpp_source "${TEST_BINARY_DIR}/generated.cpp")
execute_process(
  COMMAND "${MPFC}" --target cpp --no-banner "${INPUT}" -o "${cpp_source}"
  RESULT_VARIABLE cpp_transpile_status
  OUTPUT_VARIABLE cpp_transpile_output
  ERROR_VARIABLE cpp_transpile_error)
require_success("C++17 transpilation" "${cpp_transpile_status}"
                "${cpp_transpile_output}" "${cpp_transpile_error}")

set(configure_arguments
  -S "${TEST_SOURCE_DIR}"
  -B "${TEST_BINARY_DIR}/cpp-project"
  "-DGENERATED_SOURCE=${cpp_source}"
  -DGENERATED_COMPILE_ONLY=OFF
  -DCMAKE_BUILD_TYPE=Release)
if(DEFINED CXX_COMPILER AND NOT CXX_COMPILER STREQUAL "")
  list(APPEND configure_arguments "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
  list(APPEND configure_arguments -G "${GENERATOR}")
endif()
if(DEFINED GENERATOR_PLATFORM AND NOT GENERATOR_PLATFORM STREQUAL "")
  list(APPEND configure_arguments -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT GENERATOR_TOOLSET STREQUAL "")
  list(APPEND configure_arguments -T "${GENERATOR_TOOLSET}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" ${configure_arguments}
  RESULT_VARIABLE cpp_configure_status
  OUTPUT_VARIABLE cpp_configure_output
  ERROR_VARIABLE cpp_configure_error)
require_success("generated C++ configure" "${cpp_configure_status}"
                "${cpp_configure_output}" "${cpp_configure_error}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${TEST_BINARY_DIR}/cpp-project" --config Release --parallel
  RESULT_VARIABLE cpp_build_status
  OUTPUT_VARIABLE cpp_build_output
  ERROR_VARIABLE cpp_build_error)
require_success("generated C++ build" "${cpp_build_status}"
                "${cpp_build_output}" "${cpp_build_error}")

set(cpp_program "")
foreach(candidate
    "${TEST_BINARY_DIR}/cpp-project/mpf-generated-program"
    "${TEST_BINARY_DIR}/cpp-project/mpf-generated-program.exe"
    "${TEST_BINARY_DIR}/cpp-project/Release/mpf-generated-program"
    "${TEST_BINARY_DIR}/cpp-project/Release/mpf-generated-program.exe")
  if(EXISTS "${candidate}")
    set(cpp_program "${candidate}")
    break()
  endif()
endforeach()
if(cpp_program STREQUAL "")
  message(FATAL_ERROR "${CASE_NAME}: generated C++ executable was not found")
endif()
execute_process(
  COMMAND "${cpp_program}"
  RESULT_VARIABLE cpp_run_status
  OUTPUT_VARIABLE cpp_run_output
  ERROR_VARIABLE cpp_run_error)
require_success("generated C++ execution" "${cpp_run_status}"
                "${cpp_run_output}" "${cpp_run_error}")
require_warning("generated C++ execution" "${cpp_run_error}")
normalize_output("${cpp_run_output}" cpp_output)

set(source_output "<not-run>")
set(source_available FALSE)
if(DEFINED SOURCE_KIND AND SOURCE_KIND STREQUAL "python" AND
   DEFINED SOURCE_EXECUTABLE AND NOT SOURCE_EXECUTABLE STREQUAL "")
  execute_process(
    COMMAND "${SOURCE_EXECUTABLE}" "${INPUT}"
    RESULT_VARIABLE source_run_status
    OUTPUT_VARIABLE source_run_output
    ERROR_VARIABLE source_run_error)
  require_success("CPython execution" "${source_run_status}"
                  "${source_run_output}" "${source_run_error}")
  normalize_output("${source_run_output}" source_output)
  set(source_available TRUE)
elseif(DEFINED SOURCE_KIND AND SOURCE_KIND STREQUAL "fortran" AND
       DEFINED SOURCE_EXECUTABLE AND NOT SOURCE_EXECUTABLE STREQUAL "")
  if(NOT DEFINED FORTRAN_STANDARD OR FORTRAN_STANDARD STREQUAL "")
    set(FORTRAN_STANDARD f2018)
  endif()
  set(source_program "${TEST_BINARY_DIR}/source-fortran-program")
  execute_process(
    COMMAND "${SOURCE_EXECUTABLE}" "-std=${FORTRAN_STANDARD}" -Wall -Wextra -Werror
            "${INPUT}" -o "${source_program}"
    RESULT_VARIABLE source_compile_status
    OUTPUT_VARIABLE source_compile_output
    ERROR_VARIABLE source_compile_error)
  require_success("source Fortran compilation" "${source_compile_status}"
                  "${source_compile_output}" "${source_compile_error}")
  execute_process(
    COMMAND "${source_program}"
    RESULT_VARIABLE source_run_status
    OUTPUT_VARIABLE source_run_output
    ERROR_VARIABLE source_run_error)
  require_success("source Fortran execution" "${source_run_status}"
                  "${source_run_output}" "${source_run_error}")
  normalize_output("${source_run_output}" source_output)
  set(source_available TRUE)
elseif(DEFINED SOURCE_KIND AND SOURCE_KIND STREQUAL "typescript" AND
       DEFINED SOURCE_EXECUTABLE AND NOT SOURCE_EXECUTABLE STREQUAL "")
  execute_process(
    COMMAND "${SOURCE_EXECUTABLE}" "${INPUT}"
    RESULT_VARIABLE source_run_status
    OUTPUT_VARIABLE source_run_output
    ERROR_VARIABLE source_run_error)
  require_success("source TypeScript execution" "${source_run_status}"
                  "${source_run_output}" "${source_run_error}")
  normalize_output("${source_run_output}" source_output)
  set(source_available TRUE)
endif()

normalize_output("${EXPECTED_OUTPUT}" expected_output)
if(DEFINED EXPECTED_WARNING)
  set(expected_warning "${EXPECTED_WARNING}")
else()
  set(expected_warning "")
endif()
record_text("${expected_warning}" recorded_expected_warning)
record_text("${javascript_run_error}" recorded_javascript_stderr)
record_text("${cpp_run_error}" recorded_cpp_stderr)
file(WRITE "${TEST_BINARY_DIR}/differential-result.txt"
  "case=${CASE_NAME}\n"
  "input=${INPUT}\n"
  "mode=${OUTPUT_MODE}\n"
  "node=${NODE}\n"
  "source-kind=${SOURCE_KIND}\n"
  "source-executable=${SOURCE_EXECUTABLE}\n"
  "fortran-standard=${FORTRAN_STANDARD}\n"
  "cxx-compiler=${CXX_COMPILER}\n"
  "generator=${GENERATOR}\n"
  "expected=${expected_output}\n"
  "expected-warning=${recorded_expected_warning}\n"
  "source=${source_output}\n"
  "javascript=${javascript_output}\n"
  "javascript-stderr=${recorded_javascript_stderr}\n"
  "cpp=${cpp_output}\n"
  "cpp-stderr=${recorded_cpp_stderr}\n")

if(NOT cpp_output STREQUAL expected_output)
  message(FATAL_ERROR
    "${CASE_NAME}: generated C++ differs from oracle: '${cpp_output}' != '${expected_output}'")
endif()
if(javascript_available AND NOT javascript_output STREQUAL cpp_output)
  message(FATAL_ERROR
    "${CASE_NAME}: JavaScript/C++ differential mismatch: '${javascript_output}' != '${cpp_output}'")
endif()
if(source_available AND NOT source_output STREQUAL cpp_output)
  message(FATAL_ERROR
    "${CASE_NAME}: source/target differential mismatch: '${source_output}' != '${cpp_output}'")
endif()
