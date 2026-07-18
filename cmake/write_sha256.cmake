cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "write_sha256.cmake requires INPUT and OUTPUT")
endif()
if(NOT EXISTS "${INPUT}")
  message(FATAL_ERROR "cannot checksum missing file: ${INPUT}")
endif()

file(SHA256 "${INPUT}" digest)
get_filename_component(filename "${INPUT}" NAME)
# Avoid a trailing newline so Windows cannot translate it to CRLF. The checksum
# remains valid coreutils format and can be consumed by Unix checksum tools.
file(WRITE "${OUTPUT}" "${digest}  ${filename}")
