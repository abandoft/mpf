cmake_minimum_required(VERSION 3.20)

project(
  mpf
  VERSION 0.4.3
  DESCRIPTION "Matlab, Python, Fortran and TypeScript to JavaScript and C++17 transpiler"
  LANGUAGES C CXX)

if(PROJECT_VERSION VERSION_LESS "0.0.1" OR PROJECT_VERSION_PATCH GREATER 9)
  message(FATAL_ERROR
    "MPF versions start at 0.0.1 and use one decimal patch digit; "
    "after 0.x.9 increment the minor component")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/include/mpf")
configure_file(
  include/mpf/version.hpp.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/include/mpf/version.hpp"
  @ONLY)

if(PROJECT_IS_TOP_LEVEL)
  file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/build" MPF_ALLOWED_BUILD_ROOT)
  file(REAL_PATH "${CMAKE_BINARY_DIR}" MPF_ACTUAL_BUILD_DIR)
  string(FIND "${MPF_ACTUAL_BUILD_DIR}/" "${MPF_ALLOWED_BUILD_ROOT}/" MPF_BUILD_ROOT_POSITION)
  if(NOT MPF_BUILD_ROOT_POSITION EQUAL 0)
    message(FATAL_ERROR
      "MPF keeps all generated files under ${MPF_ALLOWED_BUILD_ROOT}. "
      "Configure with: cmake -S . -B build/<configuration>")
  endif()
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(MPF_BUILD_CLI "Build the mpfc command-line application" ON)
option(MPF_BUILD_TESTS "Build MPF tests" ${PROJECT_IS_TOP_LEVEL})
option(MPF_BUILD_FUZZERS "Build Clang/libFuzzer targets" OFF)
option(MPF_ENABLE_WERROR "Treat compiler warnings as errors" OFF)
option(MPF_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MPF_ENABLE_CLANG_TIDY "Run clang-tidy while compiling C++ targets" OFF)
option(MPF_ENABLE_COVERAGE "Enable Clang source-based code coverage" OFF)
option(MPF_ENABLE_JAVASCRIPT_BACKEND "Build the JavaScript backend" ON)
option(MPF_ENABLE_CPP_BACKEND "Build the C++17 backend" ON)
option(MPF_REQUIRE_DIFFERENTIAL_RUNTIME
  "Require Node.js and Python when differential tests are enabled" OFF)

find_program(MPF_CLANG_FORMAT_EXECUTABLE NAMES clang-format)
add_custom_target(mpf-format
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DCLANG_FORMAT=${MPF_CLANG_FORMAT_EXECUTABLE}
    -DMODE=write
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_format.cmake
  COMMENT "Formatting MPF C++ sources")
add_custom_target(mpf-format-check
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DCLANG_FORMAT=${MPF_CLANG_FORMAT_EXECUTABLE}
    -DMODE=check
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_format.cmake
  COMMENT "Checking MPF C++ source formatting")

if(MPF_ENABLE_CLANG_TIDY)
  find_program(MPF_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
  set(CMAKE_CXX_CLANG_TIDY
    "${MPF_CLANG_TIDY_EXECUTABLE};--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
endif()

if(MPF_ENABLE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "MPF_ENABLE_COVERAGE requires Clang")
  endif()
  if(MPF_ENABLE_SANITIZERS)
    message(FATAL_ERROR "MPF_ENABLE_COVERAGE and MPF_ENABLE_SANITIZERS are separate build modes")
  endif()
  find_program(MPF_LLVM_PROFDATA_EXECUTABLE NAMES llvm-profdata REQUIRED)
  find_program(MPF_LLVM_COV_EXECUTABLE NAMES llvm-cov REQUIRED)
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate)
endif()

function(mpf_configure_compilation target visibility)
  target_compile_features(${target} ${visibility} cxx_std_17)
  target_include_directories(${target}
    ${visibility}
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    if(MPF_ENABLE_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(MPF_ENABLE_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
  if(MPF_ENABLE_SANITIZERS)
    if(MSVC)
      message(FATAL_ERROR "MPF_ENABLE_SANITIZERS is not supported with MSVC")
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} INTERFACE -fsanitize=address,undefined)
  endif()
endfunction()

add_library(mpf_core
  src/core/compilation_report.cpp
  src/core/diagnostic.cpp
  src/source/source_text.cpp
  src/source/source_manager.cpp
  src/lexer/lexer.cpp
  src/lexer/scanner.cpp
  src/lexer/python_statement_lexer.cpp
  src/lexer/matlab_statement_lexer.cpp
  src/lexer/fortran_statement_lexer.cpp
  src/lexer/typescript_statement_lexer.cpp
  src/compiler/expression.cpp
  src/compiler/intrinsic.cpp
  src/compiler/code_binding.cpp
  src/ir/dump.cpp
  src/ir/hir.cpp
  src/ir/alias_effect_analysis.cpp
  src/ir/semantic_table.cpp
  src/ir/storage_region.cpp
  src/ir/mir.cpp
  src/ir/mir_optimization.cpp
  src/ir/mir_verifier.cpp
  src/semantic/analyzer.cpp
  src/semantic/expression_analyzer.cpp
  src/semantic/flow_analysis.cpp
  src/semantic/name_analysis.cpp
  src/lexers/python_lexer.cpp
  src/lexers/matlab_lexer.cpp
  src/lexers/fortran_lexer.cpp
  src/lexers/typescript_lexer.cpp
  src/frontends/python_logical_source.cpp
  src/frontends/frontend_ast.cpp
  src/frontends/frontend_conformance.cpp
  src/frontends/frontend_registry.cpp
  src/frontends/matlab_logical_source.cpp
  src/frontends/python_statement_parser.cpp
  src/frontends/matlab_statement_parser.cpp
  src/frontends/fortran_statement_parser.cpp
  src/frontends/typescript_statement_parser.cpp
  src/frontends/python_frontend.cpp
  src/frontends/matlab_frontend.cpp
  src/frontends/fortran_source_form.cpp
  src/frontends/fortran_frontend.cpp
  src/frontends/typescript_frontend.cpp)
add_library(mpf::core ALIAS mpf_core)
set_target_properties(mpf_core PROPERTIES OUTPUT_NAME mpf-core EXPORT_NAME core)
mpf_configure_compilation(mpf_core PUBLIC)

set(MPF_INSTALL_TARGETS mpf_core)

if(MPF_ENABLE_JAVASCRIPT_BACKEND OR MPF_ENABLE_CPP_BACKEND)
  add_library(mpf_backend_common
    src/backends/backend_artifact.cpp
    src/backends/backend_pipeline.cpp
    src/backends/identifier_mangler.cpp)
  add_library(mpf::backend-common ALIAS mpf_backend_common)
  set_target_properties(mpf_backend_common PROPERTIES
    OUTPUT_NAME mpf-backend-common
    EXPORT_NAME backend-common)
  target_link_libraries(mpf_backend_common PUBLIC mpf::core)
  mpf_configure_compilation(mpf_backend_common PUBLIC)
  list(APPEND MPF_INSTALL_TARGETS mpf_backend_common)
endif()

if(MPF_ENABLE_JAVASCRIPT_BACKEND)
  add_library(mpf_backend_javascript
    src/backends/javascript_backend.cpp
    src/backends/javascript_bindings.cpp
    src/backends/javascript_emitter.cpp
    src/backends/javascript_lir_planning.cpp
    src/backends/javascript_lir_representation.cpp
    src/backends/javascript_lowering.cpp
    src/backends/javascript_renderer.cpp
    src/backends/javascript_runtime.cpp
    src/backends/javascript_validator.cpp)
  add_library(mpf::backend-javascript ALIAS mpf_backend_javascript)
  set_target_properties(mpf_backend_javascript PROPERTIES
    OUTPUT_NAME mpf-backend-javascript
    EXPORT_NAME backend-javascript)
  target_link_libraries(mpf_backend_javascript PUBLIC mpf::backend-common)
  mpf_configure_compilation(mpf_backend_javascript PUBLIC)
  list(APPEND MPF_INSTALL_TARGETS mpf_backend_javascript)
endif()

if(MPF_ENABLE_CPP_BACKEND)
  add_library(mpf_backend_cpp
    src/backends/cpp_backend.cpp
    src/backends/cpp_bindings.cpp
    src/backends/cpp_emitter.cpp
    src/backends/cpp_lir_planning.cpp
    src/backends/cpp_lir_representation.cpp
    src/backends/cpp_lowering.cpp
    src/backends/cpp_renderer.cpp
    src/backends/cpp_runtime.cpp
    src/backends/cpp_validator.cpp)
  add_library(mpf::backend-cpp ALIAS mpf_backend_cpp)
  set_target_properties(mpf_backend_cpp PROPERTIES
    OUTPUT_NAME mpf-backend-cpp
    EXPORT_NAME backend-cpp)
  target_link_libraries(mpf_backend_cpp PUBLIC mpf::backend-common)
  mpf_configure_compilation(mpf_backend_cpp PUBLIC)
  list(APPEND MPF_INSTALL_TARGETS mpf_backend_cpp)
endif()

add_library(mpf
  src/backends/backend_conformance.cpp
  src/core/backend_registry.cpp
  src/core/source_map.cpp
  src/core/transpiler.cpp)
add_library(mpf::mpf ALIAS mpf)
set_target_properties(mpf PROPERTIES OUTPUT_NAME mpf EXPORT_NAME mpf)
target_link_libraries(mpf PUBLIC mpf::core)
if(MPF_ENABLE_JAVASCRIPT_BACKEND)
  target_link_libraries(mpf PUBLIC mpf::backend-javascript)
  target_compile_definitions(mpf PRIVATE MPF_HAS_JAVASCRIPT_BACKEND=1)
else()
  target_compile_definitions(mpf PRIVATE MPF_HAS_JAVASCRIPT_BACKEND=0)
endif()
if(MPF_ENABLE_CPP_BACKEND)
  target_link_libraries(mpf PUBLIC mpf::backend-cpp)
  target_compile_definitions(mpf PRIVATE MPF_HAS_CPP_BACKEND=1)
else()
  target_compile_definitions(mpf PRIVATE MPF_HAS_CPP_BACKEND=0)
endif()
mpf_configure_compilation(mpf PUBLIC)
list(APPEND MPF_INSTALL_TARGETS mpf)

if(MPF_BUILD_CLI)
  add_executable(mpfc src/cli/main.cpp)
  target_link_libraries(mpfc PRIVATE mpf::mpf)
  target_compile_features(mpfc PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(mpfc PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(mpfc PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
endif()

if(MPF_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()

if(MPF_ENABLE_COVERAGE)
  if(NOT MPF_BUILD_TESTS OR NOT MPF_BUILD_CLI)
    message(FATAL_ERROR "MPF_ENABLE_COVERAGE requires MPF_BUILD_TESTS and MPF_BUILD_CLI")
  endif()
  add_custom_target(mpf-coverage
    COMMAND ${CMAKE_COMMAND} -E rm -rf
      ${CMAKE_CURRENT_BINARY_DIR}/profiles
      ${CMAKE_CURRENT_BINARY_DIR}/coverage
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/profiles
    COMMAND ${CMAKE_COMMAND} -E env
      LLVM_PROFILE_FILE=${CMAKE_CURRENT_BINARY_DIR}/profiles/%p-%m.profraw
      ${CMAKE_CTEST_COMMAND}
      --test-dir ${CMAKE_BINARY_DIR}
      --build-config $<CONFIG>
      --label-exclude coverage-excluded
      --output-on-failure
    COMMAND ${CMAKE_COMMAND}
      -DPROFILE_DIR=${CMAKE_CURRENT_BINARY_DIR}/profiles
      -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/coverage
      -DLLVM_PROFDATA=${MPF_LLVM_PROFDATA_EXECUTABLE}
      -DLLVM_COV=${MPF_LLVM_COV_EXECUTABLE}
      -DPRIMARY_OBJECT=$<TARGET_FILE:mpf-tests>
      -DEXTRA_OBJECT=$<TARGET_FILE:mpfc>
      -DFUZZ_OBJECT=$<TARGET_FILE:mpf-fuzz-smoke>
      -DMIN_LINE_PERCENT=85
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_coverage.cmake
    DEPENDS mpf-tests mpfc mpf-fuzz-smoke
    USES_TERMINAL
    COMMENT "Running tests and enforcing MPF source coverage")
endif()

install(TARGETS ${MPF_INSTALL_TARGETS}
  EXPORT mpf-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(MPF_BUILD_CLI)
  install(TARGETS mpfc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/include/mpf/version.hpp"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mpf)
install(FILES docs/schemas/diagnostics-v1.schema.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/mpf/schemas)
install(EXPORT mpf-targets
  FILE mpf-targets.cmake
  NAMESPACE mpf::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mpf)

configure_package_config_file(
  cmake/mpf-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/mpf-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mpf)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/mpf-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/mpf-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/mpf-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mpf)
