cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MPFC OR NOT DEFINED INPUT OR NOT DEFINED TEST_SOURCE_DIR OR
   NOT DEFINED TEST_BINARY_DIR)
  message(FATAL_ERROR "verify_generated_cpp_compile.cmake received incomplete arguments")
endif()

file(REMOVE_RECURSE "${TEST_BINARY_DIR}")
file(MAKE_DIRECTORY "${TEST_BINARY_DIR}")
set(generated_source "${TEST_BINARY_DIR}/generated.cpp")

execute_process(
  COMMAND "${MPFC}" --target cpp --no-banner "${INPUT}" -o "${generated_source}"
  RESULT_VARIABLE transpile_result
  OUTPUT_VARIABLE transpile_output
  ERROR_VARIABLE transpile_error)
if(NOT transpile_result EQUAL 0)
  message(FATAL_ERROR "mpfc failed (${transpile_result}): ${transpile_error}${transpile_output}")
endif()

set(configure_arguments
  -S "${TEST_SOURCE_DIR}"
  -B "${TEST_BINARY_DIR}/project"
  "-DGENERATED_SOURCE=${generated_source}"
  -DGENERATED_COMPILE_ONLY=ON
  -DCMAKE_BUILD_TYPE=Release)
include("${CMAKE_CURRENT_LIST_DIR}/generated_toolchain.cmake")
mpf_append_generated_toolchain(configure_arguments)

execute_process(
  COMMAND "${CMAKE_COMMAND}" ${configure_arguments}
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "generated C++ configure failed: ${configure_error}${configure_output}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${TEST_BINARY_DIR}/project" --config Release --parallel
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "generated C++ build failed: ${build_error}${build_output}")
endif()
