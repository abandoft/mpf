cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED BUILD_DIR OR NOT DEFINED TAG)
  message(FATAL_ERROR "release version verification requires BUILD_DIR and TAG")
endif()

file(STRINGS "${BUILD_DIR}/CMakeCache.txt" project_version_line
  REGEX "^CMAKE_PROJECT_VERSION:STATIC=")
if(NOT project_version_line)
  message(FATAL_ERROR "configured project version is missing from ${BUILD_DIR}/CMakeCache.txt")
endif()
string(REGEX REPLACE "^[^=]+=" "" project_version "${project_version_line}")

if(NOT TAG STREQUAL "${project_version}")
  message(FATAL_ERROR
    "release tag '${TAG}' does not match configured MPF version '${project_version}'")
endif()

function(mpf_verify_changelog changelog_path changelog_name result_variable)
  file(READ "${changelog_path}" changelog)
  string(REPLACE "\r\n" "\n" changelog "${changelog}")
  string(REPLACE "\r" "\n" changelog "${changelog}")
  string(FIND "${changelog}" "## ${project_version}\n" release_heading)
  if(NOT release_heading EQUAL 0)
    message(FATAL_ERROR
      "${changelog_name} must begin with the configured release section ## ${project_version}")
  endif()
  string(SUBSTRING "${changelog}" ${release_heading} -1 release_and_history)
  string(FIND "${release_and_history}" "\n## " next_heading)
  if(next_heading LESS 0)
    set(release_section "${release_and_history}")
  else()
    string(SUBSTRING "${release_and_history}" 0 ${next_heading} release_section)
  endif()
  string(REPLACE ";" "\\;" release_section_for_count "${release_section}")
  string(REGEX MATCHALL "\n- [^\n]+" release_entries "${release_section_for_count}")
  list(LENGTH release_entries release_entry_count)
  if(release_entry_count LESS 8 OR release_entry_count GREATER 20)
    message(FATAL_ERROR
      "release ${project_version} in ${changelog_name} must contain 8-20 entries; "
      "found ${release_entry_count}")
  endif()
  set(${result_variable} "${release_entry_count}" PARENT_SCOPE)
endfunction()

mpf_verify_changelog(
  "${CMAKE_CURRENT_LIST_DIR}/../CHANGELOG.md" "CHANGELOG.md" english_entry_count)
mpf_verify_changelog(
  "${CMAKE_CURRENT_LIST_DIR}/../CHANGELOG-ZH.md" "CHANGELOG-ZH.md" chinese_entry_count)
if(NOT english_entry_count EQUAL chinese_entry_count)
  message(FATAL_ERROR
    "release ${project_version} changelog entry counts differ: "
    "CHANGELOG.md=${english_entry_count}, CHANGELOG-ZH.md=${chinese_entry_count}")
endif()

message(STATUS
  "MPF release version contract passed for ${TAG} with ${english_entry_count} entries")
