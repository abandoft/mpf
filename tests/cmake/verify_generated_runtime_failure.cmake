foreach(required MPFC INPUT CASE_NAME TEST_SOURCE_DIR TEST_BINARY_DIR EXPECTED_ERROR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "verify_generated_runtime_failure.cmake requires ${required}")
  endif()
endforeach()

function(require_success stage status stdout stderr)
  if(NOT "${status}" STREQUAL "0")
    message(FATAL_ERROR
      "${CASE_NAME}: ${stage} failed (${status})\nstdout:\n${stdout}\nstderr:\n${stderr}")
  endif()
endfunction()

function(require_failure stage status stdout stderr)
  if("${status}" STREQUAL "0")
    message(FATAL_ERROR "${CASE_NAME}: ${stage} unexpectedly succeeded\nstdout:\n${stdout}")
  endif()
  set(combined "${stdout}\n${stderr}")
  string(FIND "${combined}" "${EXPECTED_ERROR}" error_offset)
  if(error_offset EQUAL -1)
    message(FATAL_ERROR
      "${CASE_NAME}: ${stage} did not report '${EXPECTED_ERROR}'\nstdout:\n${stdout}\nstderr:\n${stderr}")
  endif()
endfunction()

file(REMOVE_RECURSE "${TEST_BINARY_DIR}")
file(MAKE_DIRECTORY "${TEST_BINARY_DIR}")

if(DEFINED NODE AND NOT NODE STREQUAL "" AND NOT NODE MATCHES "-NOTFOUND$")
  set(javascript_source "${TEST_BINARY_DIR}/generated.mjs")
  execute_process(
    COMMAND "${MPFC}" --target javascript --no-banner "${INPUT}" -o "${javascript_source}"
    RESULT_VARIABLE javascript_transpile_status
    OUTPUT_VARIABLE javascript_transpile_output
    ERROR_VARIABLE javascript_transpile_error)
  require_success("JavaScript transpilation" "${javascript_transpile_status}"
                  "${javascript_transpile_output}" "${javascript_transpile_error}")
  execute_process(
    COMMAND "${NODE}" "${javascript_source}"
    RESULT_VARIABLE javascript_run_status
    OUTPUT_VARIABLE javascript_run_output
    ERROR_VARIABLE javascript_run_error)
  require_failure("JavaScript execution" "${javascript_run_status}"
                  "${javascript_run_output}" "${javascript_run_error}")
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
require_failure("generated C++ execution" "${cpp_run_status}"
                "${cpp_run_output}" "${cpp_run_error}")
