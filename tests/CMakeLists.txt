add_executable(mpf-tests
  test_main.cpp
  unit/api_tests.cpp
  unit/compiler_foundation_tests.cpp
  unit/extension_architecture_tests.cpp
  unit/multilevel_ir_tests.cpp
  unit/semantic_tests.cpp
  integration/language_tests.cpp
  integration/control_flow_tests.cpp
  integration/array_tests.cpp)
target_link_libraries(mpf-tests PRIVATE mpf::mpf)
target_compile_features(mpf-tests PRIVATE cxx_std_17)
target_include_directories(mpf-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(mpf-tests PRIVATE MPF_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
  target_compile_options(mpf-tests PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(mpf-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(mpf-fuzz-smoke fuzz/fuzz_smoke.cpp)
target_link_libraries(mpf-fuzz-smoke PRIVATE mpf::mpf)
target_compile_features(mpf-fuzz-smoke PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(mpf-fuzz-smoke PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(mpf-fuzz-smoke PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
add_test(NAME mpf.fuzz.smoke
  COMMAND mpf-fuzz-smoke ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
set_tests_properties(mpf.fuzz.smoke PROPERTIES LABELS "fuzz" TIMEOUT 120)

find_package(Threads REQUIRED)
add_executable(mpf-benchmark performance/benchmark.cpp)
target_link_libraries(mpf-benchmark PRIVATE mpf::mpf Threads::Threads)
target_compile_features(mpf-benchmark PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(mpf-benchmark PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(mpf-benchmark PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
if(NOT MPF_ENABLE_SANITIZERS)
  add_test(NAME mpf.performance.release-gate
    COMMAND ${CMAKE_COMMAND}
      -DBENCHMARK=$<TARGET_FILE:mpf-benchmark>
      -DBASELINE=${CMAKE_CURRENT_SOURCE_DIR}/performance/baseline.json
      -DREPORT=${CMAKE_BINARY_DIR}/performance-report.json
      -DPROJECT_VERSION=${PROJECT_VERSION}
      -P ${PROJECT_SOURCE_DIR}/cmake/verify_performance.cmake)
  set_tests_properties(mpf.performance.release-gate PROPERTIES
    LABELS "performance;coverage-excluded"
    RUN_SERIAL TRUE
    TIMEOUT 180)
endif()
add_custom_target(mpf-performance
  COMMAND ${CMAKE_COMMAND}
    -DBENCHMARK=$<TARGET_FILE:mpf-benchmark>
    -DBASELINE=${CMAKE_CURRENT_SOURCE_DIR}/performance/baseline.json
    -DREPORT=${CMAKE_BINARY_DIR}/performance-report.json
    -DPROJECT_VERSION=${PROJECT_VERSION}
    -P ${PROJECT_SOURCE_DIR}/cmake/verify_performance.cmake
  DEPENDS mpf-benchmark
  USES_TERMINAL)

if(MPF_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "MPF_BUILD_FUZZERS requires Clang/libFuzzer")
  endif()
  add_executable(mpf-transpiler-fuzzer fuzz/transpiler_fuzzer.cpp)
  target_link_libraries(mpf-transpiler-fuzzer PRIVATE mpf::mpf)
  target_compile_features(mpf-transpiler-fuzzer PRIVATE cxx_std_17)
  target_compile_options(mpf-transpiler-fuzzer PRIVATE
    -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
  target_link_options(mpf-transpiler-fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

add_test(NAME mpf.unit-and-integration COMMAND mpf-tests)
add_test(NAME mpf.architecture.compiler-layers
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_compiler_layers.cmake)

function(mpf_add_generated_cpp_compile_test name input)
  add_test(NAME mpf.generated.cpp.${name}
    COMMAND ${CMAKE_COMMAND}
      -DMPFC=$<TARGET_FILE:mpfc>
      -DINPUT=${PROJECT_SOURCE_DIR}/${input}
      -DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/generated
      -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/generated-cpp-${name}
      "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_generated_cpp_compile.cmake)
  set_tests_properties(mpf.generated.cpp.${name} PROPERTIES
    LABELS "generated;cpp"
    TIMEOUT 300)
endfunction()

function(mpf_add_differential_case name language input expected_output)
  if(ARGC GREATER 4)
    set(output_mode "${ARGV4}")
  else()
    set(output_mode tokens)
  endif()
  set(source_kind none)
  set(source_executable "")
  if(language STREQUAL "python" AND Python3_Interpreter_FOUND)
    set(source_kind python)
    set(source_executable "${Python3_EXECUTABLE}")
  elseif(language STREQUAL "fortran" AND MPF_GFORTRAN_EXECUTABLE AND NOT WIN32)
    set(source_kind fortran)
    set(source_executable "${MPF_GFORTRAN_EXECUTABLE}")
  elseif(language STREQUAL "typescript" AND MPF_NODE_EXECUTABLE)
    set(source_kind typescript)
    set(source_executable "${MPF_NODE_EXECUTABLE}")
  endif()

  set(tool_arguments)
  if(MPF_NODE_EXECUTABLE)
    list(APPEND tool_arguments "-DNODE=${MPF_NODE_EXECUTABLE}")
  endif()
  if(NOT source_executable STREQUAL "")
    list(APPEND tool_arguments "-DSOURCE_EXECUTABLE=${source_executable}")
  endif()
  if(source_kind STREQUAL "fortran")
    list(APPEND tool_arguments "-DFORTRAN_STANDARD=${MPF_FORTRAN_REFERENCE_STANDARD}")
  endif()

  add_test(NAME mpf.differential.${name}
    COMMAND ${CMAKE_COMMAND}
      -DMPFC=$<TARGET_FILE:mpfc>
      -DINPUT=${PROJECT_SOURCE_DIR}/${input}
      -DCASE_NAME=${name}
      -DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/generated
      -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/differential/${name}
      "-DEXPECTED_OUTPUT=${expected_output}"
      -DOUTPUT_MODE=${output_mode}
      -DSOURCE_KIND=${source_kind}
      "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      ${tool_arguments}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_differential.cmake)
  set_tests_properties(mpf.differential.${name} PROPERTIES
    LABELS "differential;${language}"
    TIMEOUT 300)
endfunction()

function(mpf_add_backend_isolation_test name javascript cpp)
  add_test(NAME mpf.backend-isolation.${name}
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
      -DBUILD_DIR=${CMAKE_BINARY_DIR}/backend-isolation-${name}
      -DENABLE_JAVASCRIPT=${javascript}
      -DENABLE_CPP=${cpp}
      -DPROJECT_VERSION=${PROJECT_VERSION}
      -DCONFIG=Release
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_backend_isolation.cmake)
  set_tests_properties(mpf.backend-isolation.${name} PROPERTIES
    LABELS "isolation;coverage-excluded"
    TIMEOUT 300)
endfunction()

if(TARGET mpfc)
  add_test(NAME mpf.cli.version COMMAND mpfc --version)
  set_tests_properties(mpf.cli.version PROPERTIES
    PASS_REGULAR_EXPRESSION "mpfc ${PROJECT_VERSION}")
  add_test(NAME mpf.cli.contract
    COMMAND ${CMAKE_COMMAND}
      -DMPFC=$<TARGET_FILE:mpfc>
      -DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}
      -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
      -DSCHEMA=${PROJECT_SOURCE_DIR}/docs/schemas/diagnostics-v1.schema.json
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_cli_contract.cmake)

  find_program(MPF_NODE_EXECUTABLE node)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  find_program(MPF_GFORTRAN_EXECUTABLE gfortran)
  set(MPF_FORTRAN_REFERENCE_STANDARD "f2018" CACHE STRING
      "Strict gfortran -std mode used by the differential oracle")
  if(NOT MPF_FORTRAN_REFERENCE_STANDARD MATCHES "^f(95|2003|2008|2018|2023)$")
    message(FATAL_ERROR
      "MPF_FORTRAN_REFERENCE_STANDARD must name a strict gfortran standard mode")
  endif()
  if(MPF_REQUIRE_DIFFERENTIAL_RUNTIME AND
     (NOT MPF_NODE_EXECUTABLE OR NOT Python3_Interpreter_FOUND OR
      Python3_VERSION VERSION_LESS "3.14"))
    message(FATAL_ERROR
      "MPF_REQUIRE_DIFFERENTIAL_RUNTIME requires Node.js and Python 3.14+")
  endif()

  include(${CMAKE_CURRENT_SOURCE_DIR}/differential/corpus.cmake)
  mpf_add_generated_cpp_compile_test(matlab-basic examples/matlab/basic.m)
endif()

mpf_add_backend_isolation_test(javascript-only ON OFF)
mpf_add_backend_isolation_test(cpp-only OFF ON)
mpf_add_backend_isolation_test(core-only OFF OFF)

if(MPF_ENABLE_JAVASCRIPT_BACKEND AND MPF_ENABLE_CPP_BACKEND)
  add_test(NAME mpf.installed.frontend-backend-examples
    COMMAND ${CMAKE_COMMAND}
      -DPROJECT_BUILD_DIR=${CMAKE_BINARY_DIR}
      -DBUILD_DIR=${CMAKE_BINARY_DIR}/installed-examples
      -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
      -DSTAGE=${CMAKE_BINARY_DIR}/installed-examples-stage
      -DCONFIG=$<CONFIG>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_installed_examples.cmake)
  set_tests_properties(mpf.installed.frontend-backend-examples PROPERTIES
    LABELS "install;conformance;coverage-excluded"
    TIMEOUT 300)
endif()
