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

message(STATUS "MPF release version contract passed for ${TAG}")
