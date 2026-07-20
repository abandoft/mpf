cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_BINARY_DIR OR NOT DEFINED PROJECT_VERSION)
  message(FATAL_ERROR "SOURCE_DIR, TEST_BINARY_DIR and PROJECT_VERSION are required")
endif()
cmake_path(ABSOLUTE_PATH SOURCE_DIR BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
cmake_path(ABSOLUTE_PATH TEST_BINARY_DIR BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)

set(artifact mpf-contract)
set(fixture_root "${TEST_BINARY_DIR}/fixture/${artifact}")
set(archive "${TEST_BINARY_DIR}/${artifact}.zip")
set(checksum "${archive}.sha256")
file(REMOVE_RECURSE "${TEST_BINARY_DIR}")
file(MAKE_DIRECTORY
  "${fixture_root}/bin"
  "${fixture_root}/include/mpf"
  "${fixture_root}/lib/cmake/mpf"
  "${fixture_root}/share/doc/mpf"
  "${fixture_root}/share/mpf/schemas")
file(WRITE "${fixture_root}/bin/mpfc" "release-verifier fixture\n")
file(WRITE "${fixture_root}/include/mpf/mpf.hpp" "#pragma once\n")
file(WRITE "${fixture_root}/include/mpf/version.hpp"
  "#pragma once\n#define MPF_VERSION_STRING \"${PROJECT_VERSION}\"\n")
file(WRITE "${fixture_root}/lib/cmake/mpf/mpf-config.cmake" "# fixture\n")
file(WRITE "${fixture_root}/share/mpf/schemas/diagnostics-v1.schema.json" "{}\n")
configure_file("${SOURCE_DIR}/LICENSE" "${fixture_root}/share/doc/mpf/LICENSE" COPYONLY)

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar cf "${archive}" --format=zip "${artifact}"
  WORKING_DIRECTORY "${TEST_BINARY_DIR}/fixture"
  RESULT_VARIABLE archive_status
  ERROR_VARIABLE archive_error)
if(NOT archive_status EQUAL 0)
  message(FATAL_ERROR "release verifier fixture archive failed: ${archive_error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DINPUT=${archive}" "-DOUTPUT=${checksum}"
    -P "${SOURCE_DIR}/cmake/write_sha256.cmake"
  RESULT_VARIABLE checksum_status
  ERROR_VARIABLE checksum_error)
if(NOT checksum_status EQUAL 0)
  message(FATAL_ERROR "release verifier fixture checksum failed: ${checksum_error}")
endif()
file(SHA256 "${archive}" archive_digest)
get_filename_component(archive_name "${archive}" NAME)
file(READ "${checksum}" checksum_text)
set(expected_checksum_text "${archive_digest}  ${archive_name}")
if(NOT checksum_text STREQUAL expected_checksum_text)
  message(FATAL_ERROR
    "generated checksum is not the required portable single-line format")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DARCHIVE=${archive}"
    "-DCHECKSUM=${checksum}"
    "-DARTIFACT=${artifact}"
    "-DVERSION=${PROJECT_VERSION}"
    "-DLICENSE_FILE=${SOURCE_DIR}/LICENSE"
    "-DVERIFY_DIR=${TEST_BINARY_DIR}/verified"
    -P "${SOURCE_DIR}/cmake/verify_release_archive.cmake"
  RESULT_VARIABLE verification_status
  OUTPUT_VARIABLE verification_output
  ERROR_VARIABLE verification_error)
if(NOT verification_status EQUAL 0)
  message(FATAL_ERROR
    "valid release fixture was rejected:\n${verification_output}\n${verification_error}")
endif()

string(REPEAT "0" 64 wrong_digest)
file(WRITE "${TEST_BINARY_DIR}/wrong.sha256" "${wrong_digest}  ${artifact}.zip\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DARCHIVE=${archive}"
    "-DCHECKSUM=${TEST_BINARY_DIR}/wrong.sha256"
    "-DARTIFACT=${artifact}"
    "-DVERSION=${PROJECT_VERSION}"
    "-DLICENSE_FILE=${SOURCE_DIR}/LICENSE"
    "-DVERIFY_DIR=${TEST_BINARY_DIR}/wrong-checksum"
    -P "${SOURCE_DIR}/cmake/verify_release_archive.cmake"
  RESULT_VARIABLE wrong_checksum_status
  OUTPUT_QUIET
  ERROR_QUIET)
if(wrong_checksum_status EQUAL 0)
  message(FATAL_ERROR "release verifier accepted an invalid checksum")
endif()

string(ASCII 13 carriage_return)
set(nonportable_checksum "${TEST_BINARY_DIR}/nonportable.sha256")
file(WRITE "${nonportable_checksum}"
  "${archive_digest}  ${archive_name}${carriage_return}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DARCHIVE=${archive}"
    "-DCHECKSUM=${nonportable_checksum}"
    "-DARTIFACT=${artifact}"
    "-DVERSION=${PROJECT_VERSION}"
    "-DLICENSE_FILE=${SOURCE_DIR}/LICENSE"
    "-DVERIFY_DIR=${TEST_BINARY_DIR}/nonportable-checksum"
    -P "${SOURCE_DIR}/cmake/verify_release_archive.cmake"
  RESULT_VARIABLE nonportable_checksum_status
  OUTPUT_QUIET
  ERROR_QUIET)
if(nonportable_checksum_status EQUAL 0)
  message(FATAL_ERROR "release verifier accepted a checksum containing a carriage return")
endif()

file(READ "${SOURCE_DIR}/LICENSE" license_text)
string(REPLACE "\n" "\r\n" crlf_license "${license_text}")
file(WRITE "${fixture_root}/share/doc/mpf/LICENSE" "${crlf_license}")
set(changed_archive "${TEST_BINARY_DIR}/${artifact}-changed-license.zip")
set(changed_checksum "${changed_archive}.sha256")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar cf "${changed_archive}" --format=zip "${artifact}"
  WORKING_DIRECTORY "${TEST_BINARY_DIR}/fixture"
  RESULT_VARIABLE changed_archive_status
  ERROR_VARIABLE changed_archive_error)
if(NOT changed_archive_status EQUAL 0)
  message(FATAL_ERROR "changed-license fixture archive failed: ${changed_archive_error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DINPUT=${changed_archive}" "-DOUTPUT=${changed_checksum}"
    -P "${SOURCE_DIR}/cmake/write_sha256.cmake"
  RESULT_VARIABLE changed_checksum_status
  ERROR_VARIABLE changed_checksum_error)
if(NOT changed_checksum_status EQUAL 0)
  message(FATAL_ERROR "changed-license fixture checksum failed: ${changed_checksum_error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DARCHIVE=${changed_archive}"
    "-DCHECKSUM=${changed_checksum}"
    "-DARTIFACT=${artifact}"
    "-DVERSION=${PROJECT_VERSION}"
    "-DLICENSE_FILE=${SOURCE_DIR}/LICENSE"
    "-DVERIFY_DIR=${TEST_BINARY_DIR}/changed-license"
    -P "${SOURCE_DIR}/cmake/verify_release_archive.cmake"
  RESULT_VARIABLE changed_license_status
  OUTPUT_QUIET
  ERROR_QUIET)
if(changed_license_status EQUAL 0)
  message(FATAL_ERROR "release verifier accepted a byte-different LICENSE")
endif()

set(policy_root "${TEST_BINARY_DIR}/changelog-policy")
set(policy_build "${policy_root}/build")
file(MAKE_DIRECTORY "${policy_root}/cmake" "${policy_build}")
configure_file("${SOURCE_DIR}/cmake/verify_release_version.cmake"
  "${policy_root}/cmake/verify_release_version.cmake" COPYONLY)
configure_file("${SOURCE_DIR}/CHANGELOG.md" "${policy_root}/CHANGELOG.md" COPYONLY)
configure_file("${SOURCE_DIR}/CHANGELOG-ZH.md" "${policy_root}/CHANGELOG-ZH.md" COPYONLY)
file(WRITE "${policy_build}/CMakeCache.txt"
  "CMAKE_PROJECT_VERSION:STATIC=${PROJECT_VERSION}\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DBUILD_DIR=${policy_build}"
    "-DTAG=${PROJECT_VERSION}"
    -P "${policy_root}/cmake/verify_release_version.cmake"
  RESULT_VARIABLE valid_policy_status
  OUTPUT_VARIABLE valid_policy_output
  ERROR_VARIABLE valid_policy_error)
if(NOT valid_policy_status EQUAL 0)
  message(FATAL_ERROR
    "valid public changelogs were rejected:\n${valid_policy_output}\n${valid_policy_error}")
endif()

string(CONCAT forbidden_metadata_entry
  "\n- Production source cover"
  "age is 91%.\n")
file(APPEND "${policy_root}/CHANGELOG.md" "${forbidden_metadata_entry}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DBUILD_DIR=${policy_build}"
    "-DTAG=${PROJECT_VERSION}"
    -P "${policy_root}/cmake/verify_release_version.cmake"
  RESULT_VARIABLE historical_metadata_status
  OUTPUT_VARIABLE historical_metadata_output
  ERROR_VARIABLE historical_metadata_error)
if(historical_metadata_status EQUAL 0)
  message(FATAL_ERROR
    "release verifier accepted internal metadata in a historical changelog section")
endif()
string(CONCAT historical_metadata_log
  "${historical_metadata_output}" "${historical_metadata_error}")
if(NOT historical_metadata_log MATCHES "complete public changelog history")
  message(FATAL_ERROR
    "historical changelog rejection did not report the public-history policy")
endif()
file(REMOVE_RECURSE "${policy_root}")

message(STATUS
  "Release support scripts accepted valid assets and rejected corrupt archives and changelogs")
