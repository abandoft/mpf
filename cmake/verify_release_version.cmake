if(NOT DEFINED BUILD_DIR OR NOT DEFINED TAG)
  message(FATAL_ERROR "release version verification requires BUILD_DIR and TAG")
endif()

file(STRINGS "${BUILD_DIR}/CMakeCache.txt" project_version_line
  REGEX "^CMAKE_PROJECT_VERSION:STATIC=")
if(NOT project_version_line)
  message(FATAL_ERROR "configured project version is missing from ${BUILD_DIR}/CMakeCache.txt")
endif()
string(REGEX REPLACE "^[^=]+=" "" project_version "${project_version_line}")

if(NOT TAG STREQUAL "v${project_version}")
  message(FATAL_ERROR
    "release tag '${TAG}' does not match configured MPF version 'v${project_version}'")
endif()

file(READ "${CMAKE_CURRENT_LIST_DIR}/../CHANGELOG.md" changelog)
if(NOT changelog MATCHES "^## Unreleased\n")
  message(FATAL_ERROR "CHANGELOG.md must keep an Unreleased section before released versions")
endif()
string(FIND "${changelog}" "## ${project_version}\n" release_heading)
if(release_heading LESS 0)
  message(FATAL_ERROR
    "CHANGELOG.md has no release section for configured version ${project_version}")
endif()
string(SUBSTRING "${changelog}" ${release_heading} -1 release_and_history)
string(FIND "${release_and_history}" "\n## " next_heading)
if(next_heading LESS 0)
  set(release_section "${release_and_history}")
else()
  string(SUBSTRING "${release_and_history}" 0 ${next_heading} release_section)
endif()
string(REGEX MATCHALL "\n- [^\n]+" release_entries "${release_section}")
list(LENGTH release_entries release_entry_count)
if(release_entry_count LESS 8 OR release_entry_count GREATER 20)
  message(FATAL_ERROR
    "release ${project_version} must contain 8-20 changelog entries; "
    "found ${release_entry_count}")
endif()

message(STATUS
  "MPF release version contract passed for ${TAG} with ${release_entry_count} entries")
