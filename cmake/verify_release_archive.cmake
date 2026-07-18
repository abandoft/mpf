cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED ARCHIVE OR NOT DEFINED CHECKSUM OR NOT DEFINED ARTIFACT OR
   NOT DEFINED VERSION OR NOT DEFINED LICENSE_FILE OR NOT DEFINED VERIFY_DIR)
  message(FATAL_ERROR
    "ARCHIVE, CHECKSUM, ARTIFACT, VERSION, LICENSE_FILE and VERIFY_DIR are required")
endif()
cmake_path(ABSOLUTE_PATH ARCHIVE BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
cmake_path(ABSOLUTE_PATH CHECKSUM BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
cmake_path(ABSOLUTE_PATH LICENSE_FILE BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
cmake_path(ABSOLUTE_PATH VERIFY_DIR BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
foreach(required_file IN ITEMS "${ARCHIVE}" "${CHECKSUM}" "${LICENSE_FILE}")
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR "release verification input is missing: ${required_file}")
  endif()
endforeach()

file(SHA256 "${ARCHIVE}" actual_digest)
file(READ "${CHECKSUM}" checksum_text)
string(STRIP "${checksum_text}" checksum_text)
if(NOT checksum_text MATCHES "^([0-9A-Fa-f]+)[ \t]+[*]?([^ \t]+)$")
  message(FATAL_ERROR "invalid checksum file format: ${CHECKSUM}")
endif()
string(TOLOWER "${CMAKE_MATCH_1}" expected_digest)
get_filename_component(archive_name "${ARCHIVE}" NAME)
if(NOT expected_digest STREQUAL actual_digest OR
   NOT CMAKE_MATCH_2 STREQUAL archive_name)
  message(FATAL_ERROR
    "checksum mismatch for ${archive_name}: expected '${checksum_text}', actual '${actual_digest}'")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar tf "${ARCHIVE}"
  RESULT_VARIABLE list_status
  OUTPUT_VARIABLE archive_listing
  ERROR_VARIABLE list_error)
if(NOT list_status EQUAL 0)
  message(FATAL_ERROR "cannot list ${ARCHIVE}: ${list_error}")
endif()
string(REPLACE "\r\n" "\n" archive_listing "${archive_listing}")
string(REPLACE "\r" "\n" archive_listing "${archive_listing}")
string(REPLACE "\n" ";" archive_entries "${archive_listing}")
set(required_entries
  "${ARTIFACT}/include/mpf/mpf.hpp"
  "${ARTIFACT}/include/mpf/version.hpp"
  "${ARTIFACT}/lib/cmake/mpf/mpf-config.cmake"
  "${ARTIFACT}/share/doc/mpf/LICENSE"
  "${ARTIFACT}/share/mpf/schemas/diagnostics-v1.schema.json")
set(has_cli false)
foreach(entry IN LISTS archive_entries)
  if(entry STREQUAL "")
    continue()
  endif()
  if(NOT entry MATCHES "^${ARTIFACT}/")
    message(FATAL_ERROR "archive entry escapes the single package root: ${entry}")
  endif()
  if(entry MATCHES [[(^|/)\.\.(/|$)]] OR entry MATCHES [[(^|/)\.git(/|$)]] OR
     entry MATCHES [[(^|/)CMakeCache\.txt$]])
    message(FATAL_ERROR "archive contains a forbidden build or repository entry: ${entry}")
  endif()
  if(entry STREQUAL "${ARTIFACT}/bin/mpfc" OR
     entry STREQUAL "${ARTIFACT}/bin/mpfc.exe")
    set(has_cli true)
  endif()
endforeach()
foreach(required_entry IN LISTS required_entries)
  if(NOT required_entry IN_LIST archive_entries)
    message(FATAL_ERROR "archive is missing ${required_entry}")
  endif()
endforeach()
if(NOT has_cli)
  message(FATAL_ERROR "archive is missing the mpfc executable")
endif()

file(REMOVE_RECURSE "${VERIFY_DIR}")
file(MAKE_DIRECTORY "${VERIFY_DIR}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar xf "${ARCHIVE}"
  WORKING_DIRECTORY "${VERIFY_DIR}"
  RESULT_VARIABLE extract_status
  ERROR_VARIABLE extract_error)
if(NOT extract_status EQUAL 0)
  message(FATAL_ERROR "cannot extract ${ARCHIVE}: ${extract_error}")
endif()

set(extracted_root "${VERIFY_DIR}/${ARTIFACT}")
file(READ "${extracted_root}/include/mpf/version.hpp" version_header)
string(FIND "${version_header}" "#define MPF_VERSION_STRING \"${VERSION}\"" version_match)
if(version_match LESS 0)
  message(FATAL_ERROR "archive version header does not identify MPF ${VERSION}")
endif()
file(SHA256 "${LICENSE_FILE}" source_license_digest)
file(SHA256 "${extracted_root}/share/doc/mpf/LICENSE" archive_license_digest)
if(NOT source_license_digest STREQUAL archive_license_digest)
  message(FATAL_ERROR "archive LICENSE does not match the repository license")
endif()

message(STATUS "Verified ${archive_name} (${actual_digest})")
