cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "write_sha256.cmake requires INPUT and OUTPUT")
endif()
if(NOT EXISTS "${INPUT}")
  message(FATAL_ERROR "cannot checksum missing file: ${INPUT}")
endif()

file(SHA256 "${INPUT}" digest)
get_filename_component(filename "${INPUT}" NAME)
file(WRITE "${OUTPUT}" "${digest}  ${filename}\n")
