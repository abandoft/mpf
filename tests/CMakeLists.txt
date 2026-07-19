add_executable(mpf-tests
  test_main.cpp
  unit/api_tests.cpp
  unit/compiler_foundation_tests.cpp
  unit/extension_architecture_tests.cpp
  unit/multilevel_ir_tests.cpp
  unit/sparse_logical_lir_tests.cpp
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
    CONFIGURATIONS Release RelWithDebInfo
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

add_test(NAME mpf.release-support.contract
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/release-support-contract
    -DPROJECT_VERSION=${PROJECT_VERSION}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_release_support.cmake)
set_tests_properties(mpf.release-support.contract PROPERTIES
  LABELS "infrastructure;release;coverage-excluded"
  TIMEOUT 60)

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

function(mpf_add_generated_runtime_failure_test name input expected_error source_label)
  set(tool_arguments)
  if(MPF_NODE_EXECUTABLE)
    list(APPEND tool_arguments "-DNODE=${MPF_NODE_EXECUTABLE}")
  endif()
  add_test(NAME mpf.generated.failure.${name}
    COMMAND ${CMAKE_COMMAND}
      -DMPFC=$<TARGET_FILE:mpfc>
      -DINPUT=${PROJECT_SOURCE_DIR}/${input}
      -DCASE_NAME=${name}
      -DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/generated
      -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/generated-failure-${name}
      "-DEXPECTED_ERROR=${expected_error}"
      "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      ${tool_arguments}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_generated_runtime_failure.cmake)
  set_tests_properties(mpf.generated.failure.${name} PROPERTIES
    LABELS "generated;runtime-rejection;${source_label}"
    TIMEOUT 300)
endfunction()

function(mpf_add_generated_plan_rejection_test name input expected_error source_label)
  set(options)
  set(one_value_arguments JAVASCRIPT_SEARCH JAVASCRIPT_REPLACEMENT CPP_SEARCH CPP_REPLACEMENT)
  cmake_parse_arguments(PARSE_ARGV 4 plan "${options}" "${one_value_arguments}" "")
  foreach(required JAVASCRIPT_SEARCH JAVASCRIPT_REPLACEMENT CPP_SEARCH CPP_REPLACEMENT)
    if(NOT DEFINED plan_${required} OR plan_${required} STREQUAL "")
      message(FATAL_ERROR "${name}: generated plan rejection test requires ${required}")
    endif()
  endforeach()
  set(tool_arguments)
  if(MPF_NODE_EXECUTABLE)
    list(APPEND tool_arguments "-DNODE=${MPF_NODE_EXECUTABLE}")
  endif()
  add_test(NAME mpf.generated.failure.${name}
    COMMAND ${CMAKE_COMMAND}
      -DMPFC=$<TARGET_FILE:mpfc>
      -DINPUT=${PROJECT_SOURCE_DIR}/${input}
      -DCASE_NAME=${name}
      -DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/generated
      -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/generated-failure-${name}
      "-DEXPECTED_ERROR=${expected_error}"
      "-DJAVASCRIPT_SEARCH=${plan_JAVASCRIPT_SEARCH}"
      "-DJAVASCRIPT_REPLACEMENT=${plan_JAVASCRIPT_REPLACEMENT}"
      "-DCPP_SEARCH=${plan_CPP_SEARCH}"
      "-DCPP_REPLACEMENT=${plan_CPP_REPLACEMENT}"
      "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      ${tool_arguments}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_generated_runtime_failure.cmake)
  set_tests_properties(mpf.generated.failure.${name} PROPERTIES
    LABELS "generated;runtime-rejection;plan-corruption;${source_label}"
    TIMEOUT 300)
endfunction()

function(mpf_add_differential_case name language input expected_output)
  if(ARGC GREATER 4)
    set(output_mode "${ARGV4}")
  else()
    set(output_mode tokens)
  endif()
  if(ARGC GREATER 5)
    set(expected_warning "${ARGV5}")
  else()
    set(expected_warning "")
  endif()
  if(ARGC GREATER 6)
    set(expected_warning_count "${ARGV6}")
  elseif(expected_warning STREQUAL "")
    set(expected_warning_count 0)
  else()
    set(expected_warning_count 1)
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
      "-DEXPECTED_WARNING=${expected_warning}"
      "-DEXPECTED_WARNING_COUNT=${expected_warning_count}"
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
  mpf_add_generated_runtime_failure_test(
    matlab-dynamic-logical-mismatch tests/fixtures/matlab_dynamic_logical_mismatch.m
    "logical selector shape" matlab)
  mpf_add_generated_runtime_failure_test(
    matlab-dynamic-broadcast-mismatch tests/fixtures/matlab_dynamic_broadcast_mismatch.m
    "broadcast sizes are incompatible" matlab)
  mpf_add_generated_runtime_failure_test(
    matlab-dynamic-linear-delete-matrix tests/fixtures/matlab_dynamic_linear_delete_matrix.m
    "linear indexed deletion requires a row or column vector" matlab)
  mpf_add_generated_runtime_failure_test(
    matlab-complex-matrix-invalid-exponent
    tests/fixtures/matlab_complex_matrix_invalid_exponent.m
    "complex matrix power requires a safe integer exponent" matlab)
  mpf_add_generated_runtime_failure_test(
    matlab-sparse-triplet-out-of-bounds
    tests/fixtures/matlab_sparse_triplet_out_of_bounds.m
    "sparse triplet index exceeds the requested dimensions" matlab)
  mpf_add_generated_runtime_failure_test(
    matlab-sparse-index-out-of-bounds
    tests/fixtures/matlab_sparse_index_out_of_bounds.m
    "index out of bounds" matlab)
  mpf_add_generated_runtime_failure_test(
    matlab-sparse-assignment-nonfinite
    tests/fixtures/matlab_sparse_assignment_nonfinite.m
    "sparse assignment requires finite real or logical values" matlab)
  mpf_add_generated_plan_rejection_test(
    matlab-sparse-reshape-plan tests/fixtures/matlab_sparse_reshape_plan.m
    "result plan does not match requested dimensions" matlab
    JAVASCRIPT_SEARCH
      "__mpf_sparse_reshape(A, [2, 3], [3, 2], [3, 2])"
    JAVASCRIPT_REPLACEMENT
      "__mpf_sparse_reshape(A, [2, 3], [3, 2], [2, 3])"
    CPP_SEARCH
      "mpf_runtime::sparse_reshape(A, std::array<std::size_t, 2>{2, 3}, std::array<std::size_t, 2>{3, 2}, std::array<std::size_t, 2>{3, 2})"
    CPP_REPLACEMENT
      "mpf_runtime::sparse_reshape(A, std::array<std::size_t, 2>{2, 3}, std::array<std::size_t, 2>{3, 2}, std::array<std::size_t, 2>{2, 3})")
  mpf_add_generated_plan_rejection_test(
    matlab-logical-sparse-plan tests/fixtures/matlab_logical_sparse_plan.m
    "sparse input disagrees with its static shape contract" matlab
    JAVASCRIPT_SEARCH
      "__mpf_sparse_from_dense([[true, false], [false, true]], [2, 2], 2)"
    JAVASCRIPT_REPLACEMENT
      "__mpf_sparse_from_dense([[true, false], [false, true]], [1, 4], 2)"
    CPP_SEARCH
      "mpf_runtime::sparse_logical_from_dense(std::vector<std::vector<bool>>{std::vector<bool>{true, false}, std::vector<bool>{false, true}}, std::array<std::size_t, 2>{2, 2})"
    CPP_REPLACEMENT
      "mpf_runtime::sparse_logical_from_dense(std::vector<std::vector<bool>>{std::vector<bool>{true, false}, std::vector<bool>{false, true}}, std::array<std::size_t, 2>{1, 4})")
  mpf_add_generated_plan_rejection_test(
    matlab-sparse-logical-plan tests/fixtures/matlab_sparse_logical_plan.m
    "sparse logical storage plan is inconsistent" matlab
    JAVASCRIPT_SEARCH
      "__mpf_sparse_logical_or(A, F, [2, 2], [2, 2], [2, 2], 3, 2, 3, 2, 2)"
    JAVASCRIPT_REPLACEMENT
      "__mpf_sparse_logical_or(A, F, [2, 2], [2, 2], [2, 2], 3, 1, 3, 2, 3)"
    CPP_SEARCH
      "mpf_runtime::sparse_logical_or(A, F, std::array<std::size_t, 2>{2, 2}, std::array<std::size_t, 2>{2, 2}, std::array<std::size_t, 2>{2, 2}, 3, 2, 3, 2, 2)"
    CPP_REPLACEMENT
      "mpf_runtime::sparse_logical_or(A, F, std::array<std::size_t, 2>{2, 2}, std::array<std::size_t, 2>{2, 2}, std::array<std::size_t, 2>{2, 2}, 3, 1, 3, 2, 3)")
  mpf_add_generated_plan_rejection_test(
    matlab-sparse-product-plan tests/fixtures/matlab_sparse_product_plan.m
    "sparse matrix product shape plans are inconsistent" matlab
    JAVASCRIPT_SEARCH
      "__mpf_sparse_sparse_mtimes(A, B, [2, 3], [3, 2], [2, 2])"
    JAVASCRIPT_REPLACEMENT
      "__mpf_sparse_sparse_mtimes(A, B, [2, 3], [3, 2], [2, 3])"
    CPP_SEARCH
      "mpf_runtime::sparse_sparse_mtimes(A, B, std::array<std::size_t, 2>{2, 3}, std::array<std::size_t, 2>{3, 2}, std::array<std::size_t, 2>{2, 2})"
    CPP_REPLACEMENT
      "mpf_runtime::sparse_sparse_mtimes(A, B, std::array<std::size_t, 2>{2, 3}, std::array<std::size_t, 2>{3, 2}, std::array<std::size_t, 2>{2, 3})")
  mpf_add_generated_plan_rejection_test(
    matlab-sparse-zero-left-solve-plan tests/fixtures/matlab_sparse_zero_solve_plan.m
    "sparse left-division shape plans are inconsistent" matlab
    JAVASCRIPT_SEARCH
      "__mpf_matlab_mldivide_sparse_real_square(coefficient, right, [0, 0], [0, 3], [0, 3])"
    JAVASCRIPT_REPLACEMENT
      "__mpf_matlab_mldivide_sparse_real_square(coefficient, right, [0, 0], [0, 3], [0, 2])"
    CPP_SEARCH
      "mpf_runtime::matlab_mldivide_sparse_real_square(coefficient, right, std::array<std::size_t, 2>{0, 0}, std::array<std::size_t, 2>{0, 3}, std::array<std::size_t, 2>{0, 3})"
    CPP_REPLACEMENT
      "mpf_runtime::matlab_mldivide_sparse_real_square(coefficient, right, std::array<std::size_t, 2>{0, 0}, std::array<std::size_t, 2>{0, 3}, std::array<std::size_t, 2>{0, 2})")
  mpf_add_generated_plan_rejection_test(
    matlab-sparse-zero-right-solve-plan tests/fixtures/matlab_sparse_zero_solve_plan.m
    "sparse right-division shape plans are inconsistent" matlab
    JAVASCRIPT_SEARCH
      "__mpf_matlab_mrdivide_sparse_real_square(left, coefficient, [2, 0], [0, 0], [2, 0])"
    JAVASCRIPT_REPLACEMENT
      "__mpf_matlab_mrdivide_sparse_real_square(left, coefficient, [2, 0], [0, 0], [1, 0])"
    CPP_SEARCH
      "mpf_runtime::matlab_mrdivide_sparse_real_square(left, coefficient, std::array<std::size_t, 2>{2, 0}, std::array<std::size_t, 2>{0, 0}, std::array<std::size_t, 2>{2, 0})"
    CPP_REPLACEMENT
      "mpf_runtime::matlab_mrdivide_sparse_real_square(left, coefficient, std::array<std::size_t, 2>{2, 0}, std::array<std::size_t, 2>{0, 0}, std::array<std::size_t, 2>{1, 0})")
  mpf_add_generated_runtime_failure_test(
    matlab-sparse-scalar-nonfinite tests/fixtures/matlab_sparse_scalar_nonfinite.m
    "finite real" matlab)
  mpf_add_generated_runtime_failure_test(
    matlab-sparse-scalar-overflow tests/fixtures/matlab_sparse_scalar_overflow.m
    "sparse scalar product produced a nonfinite value" matlab)
  mpf_add_generated_runtime_failure_test(
    matlab-sparse-elementwise-nonfinite
    tests/fixtures/matlab_sparse_elementwise_nonfinite.m
    "finite real or logical" matlab)
  mpf_add_generated_runtime_failure_test(
    matlab-sparse-elementwise-overflow
    tests/fixtures/matlab_sparse_elementwise_overflow.m
    "sparse element-wise multiplication produced a nonfinite value" matlab)
  mpf_add_generated_plan_rejection_test(
    matlab-sparse-elementwise-plan tests/fixtures/matlab_sparse_elementwise_plan.m
    "sparse element-wise multiplication shape mismatch" matlab
    JAVASCRIPT_SEARCH
      "__mpf_sparse_times_dense(A, D, [1, 3], [2, 3], [2, 3])"
    JAVASCRIPT_REPLACEMENT
      "__mpf_sparse_times_dense(A, D, [1, 3], [2, 3], [1, 3])"
    CPP_SEARCH
      "mpf_runtime::sparse_times_dense(A, D, std::array<std::size_t, 2>{1, 3}, std::array<std::size_t, 2>{2, 3}, std::array<std::size_t, 2>{2, 3})"
    CPP_REPLACEMENT
      "mpf_runtime::sparse_times_dense(A, D, std::array<std::size_t, 2>{1, 3}, std::array<std::size_t, 2>{2, 3}, std::array<std::size_t, 2>{1, 3})")
  mpf_add_generated_runtime_failure_test(
    python-true-division-by-zero tests/fixtures/python_true_division_by_zero.py
    "division by zero" python)
  mpf_add_generated_runtime_failure_test(
    python-floor-division-by-zero tests/fixtures/python_floor_division_by_zero.py
    "division by zero" python)
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
      -DLICENSE_FILE=${PROJECT_SOURCE_DIR}/LICENSE
      -DINSTALL_DOCDIR=${CMAKE_INSTALL_DOCDIR}
      -DPROJECT_VERSION=${PROJECT_VERSION}
      -DCONFIG=$<CONFIG>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_installed_examples.cmake)
  set_tests_properties(mpf.installed.frontend-backend-examples PROPERTIES
    LABELS "install;conformance;coverage-excluded"
    TIMEOUT 300)
endif()
