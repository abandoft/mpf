if(NOT DEFINED STAGE OR NOT DEFINED BUILD_DIR OR NOT DEFINED SOURCE_DIR OR
   NOT DEFINED LICENSE_FILE OR NOT DEFINED INSTALL_DOCDIR OR NOT DEFINED CONFIG OR
   NOT DEFINED PROJECT_VERSION)
  message(FATAL_ERROR
    "STAGE, BUILD_DIR, SOURCE_DIR, LICENSE_FILE, INSTALL_DOCDIR, CONFIG and PROJECT_VERSION are required")
endif()
cmake_path(ABSOLUTE_PATH STAGE BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
cmake_path(ABSOLUTE_PATH BUILD_DIR BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
cmake_path(ABSOLUTE_PATH SOURCE_DIR BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
cmake_path(ABSOLUTE_PATH LICENSE_FILE BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)

set(installed_license "${STAGE}/${INSTALL_DOCDIR}/LICENSE")
if(NOT EXISTS "${installed_license}")
  message(FATAL_ERROR "installed package is missing ${INSTALL_DOCDIR}/LICENSE")
endif()
file(SHA256 "${LICENSE_FILE}" source_license_sha256)
file(SHA256 "${installed_license}" installed_license_sha256)
if(NOT source_license_sha256 STREQUAL installed_license_sha256)
  message(FATAL_ERROR "installed LICENSE does not match the repository license")
endif()

foreach(header IN ITEMS mpf.hpp diagnostic.hpp source_map.hpp transpiler.hpp version.hpp)
  if(NOT EXISTS "${STAGE}/include/mpf/${header}")
    message(FATAL_ERROR "installed package is missing include/mpf/${header}")
  endif()
endforeach()
file(GLOB_RECURSE installed_package_configs
  "${STAGE}/*/cmake/mpf/mpf-config.cmake")
list(LENGTH installed_package_configs installed_package_config_count)
if(NOT installed_package_config_count EQUAL 1)
  message(FATAL_ERROR
    "installed package must contain exactly one mpf-config.cmake; found ${installed_package_config_count}")
endif()

if(WIN32)
  set(installed_cli "${STAGE}/bin/mpfc.exe")
else()
  set(installed_cli "${STAGE}/bin/mpfc")
endif()
if(NOT EXISTS "${installed_cli}")
  message(FATAL_ERROR "installed package is missing the mpfc executable")
endif()
execute_process(
  COMMAND "${installed_cli}" --version
  RESULT_VARIABLE cli_status
  OUTPUT_VARIABLE cli_output
  ERROR_VARIABLE cli_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT cli_status EQUAL 0 OR NOT cli_output STREQUAL "mpfc ${PROJECT_VERSION}")
  message(FATAL_ERROR
    "installed mpfc version contract failed: status=${cli_status}, output='${cli_output}', error='${cli_error}'")
endif()

file(REMOVE_RECURSE "${BUILD_DIR}")
set(incompatible_source "${BUILD_DIR}/incompatible-version-source")
set(incompatible_build "${BUILD_DIR}/incompatible-version-build")
file(MAKE_DIRECTORY "${incompatible_source}")
file(WRITE "${incompatible_source}/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.20)\n"
  "project(mpf-incompatible-version-probe LANGUAGES NONE)\n"
  "find_package(mpf 99.99.9 EXACT CONFIG REQUIRED)\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${incompatible_source}" -B "${incompatible_build}"
    -DCMAKE_PREFIX_PATH=${STAGE}
  RESULT_VARIABLE incompatible_status
  OUTPUT_VARIABLE incompatible_output
  ERROR_VARIABLE incompatible_error)
if(incompatible_status EQUAL 0)
  message(FATAL_ERROR
    "installed package accepted incompatible MPF 99.99.9 request:\n"
    "${incompatible_output}\n${incompatible_error}")
endif()

foreach(example IN ITEMS frontend backend)
  set(example_build "${BUILD_DIR}/${example}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/examples/installed/${example}"
      -B "${example_build}" -DCMAKE_BUILD_TYPE=${CONFIG} -DCMAKE_PREFIX_PATH=${STAGE}
    RESULT_VARIABLE configure_status
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
  if(NOT configure_status EQUAL 0)
    message(FATAL_ERROR
      "installed ${example} example configure failed:\n${configure_output}\n${configure_error}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${example_build}" --config "${CONFIG}" --parallel
    RESULT_VARIABLE build_status
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
  if(NOT build_status EQUAL 0)
    message(FATAL_ERROR
      "installed ${example} example build failed:\n${build_output}\n${build_error}")
  endif()
  if(WIN32)
    set(executable "${example_build}/${CONFIG}/mpf-installed-${example}-example.exe")
  else()
    set(executable "${example_build}/mpf-installed-${example}-example")
  endif()
  execute_process(
    COMMAND "${executable}"
    RESULT_VARIABLE run_status
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error)
  if(NOT run_status EQUAL 0)
    message(FATAL_ERROR
      "installed ${example} example failed (${run_status}):\n${run_output}\n${run_error}")
  endif()
endforeach()

message(STATUS "MPF staged package ${PROJECT_VERSION} passed install and consumer verification")
