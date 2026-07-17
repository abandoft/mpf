if(NOT DEFINED PROJECT_BUILD_DIR OR NOT DEFINED BUILD_DIR OR NOT DEFINED SOURCE_DIR OR
   NOT DEFINED STAGE OR NOT DEFINED LICENSE_FILE OR NOT DEFINED INSTALL_DOCDIR OR
   NOT DEFINED CONFIG)
  message(FATAL_ERROR
    "PROJECT_BUILD_DIR, BUILD_DIR, SOURCE_DIR, STAGE, LICENSE_FILE, INSTALL_DOCDIR and CONFIG are required")
endif()

file(REMOVE_RECURSE "${STAGE}" "${BUILD_DIR}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BUILD_DIR}" --config "${CONFIG}"
    --prefix "${STAGE}"
  RESULT_VARIABLE install_status
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT install_status EQUAL 0)
  message(FATAL_ERROR "installed example staging failed:\n${install_output}\n${install_error}")
endif()

set(installed_license "${STAGE}/${INSTALL_DOCDIR}/LICENSE")
if(NOT EXISTS "${installed_license}")
  message(FATAL_ERROR "installed package is missing ${INSTALL_DOCDIR}/LICENSE")
endif()
file(SHA256 "${LICENSE_FILE}" source_license_sha256)
file(SHA256 "${installed_license}" installed_license_sha256)
if(NOT source_license_sha256 STREQUAL installed_license_sha256)
  message(FATAL_ERROR "installed LICENSE does not match the repository license")
endif()

set(incompatible_source "${BUILD_DIR}/incompatible-version-source")
set(incompatible_build "${BUILD_DIR}/incompatible-version-build")
file(MAKE_DIRECTORY "${incompatible_source}")
file(WRITE "${incompatible_source}/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.20)\n"
  "project(mpf-incompatible-version-probe LANGUAGES NONE)\n"
  "find_package(mpf 0.0.1 EXACT CONFIG REQUIRED)\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${incompatible_source}" -B "${incompatible_build}"
    -DCMAKE_PREFIX_PATH=${STAGE}
  RESULT_VARIABLE incompatible_status
  OUTPUT_VARIABLE incompatible_output
  ERROR_VARIABLE incompatible_error)
if(incompatible_status EQUAL 0)
  message(FATAL_ERROR
    "installed package accepted obsolete MPF 0.0.1 request despite exact-version policy:\n"
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
  execute_process(COMMAND "${executable}" RESULT_VARIABLE run_status)
  if(NOT run_status EQUAL 0)
    message(FATAL_ERROR "installed ${example} example failed (${run_status})")
  endif()
endforeach()
