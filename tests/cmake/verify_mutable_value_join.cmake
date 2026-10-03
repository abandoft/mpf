cmake_minimum_required(VERSION 3.20)

foreach(required MPFC NODE INPUT TEST_BINARY_DIR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "mutable-value join verification requires ${required}")
  endif()
endforeach()
file(MAKE_DIRECTORY "${TEST_BINARY_DIR}")
set(javascript "${TEST_BINARY_DIR}/generated.mjs")
set(source_map "${TEST_BINARY_DIR}/generated.mjs.map")
file(REMOVE "${javascript}" "${source_map}" "${TEST_BINARY_DIR}/rejected.cpp")
execute_process(
  COMMAND "${MPFC}" --target javascript --no-banner --source-map "${source_map}"
          "${INPUT}" -o "${javascript}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0 OR NOT error STREQUAL "" OR NOT output STREQUAL "")
  message(FATAL_ERROR "mutable JavaScript generation failed (${status}): ${output}${error}")
endif()
execute_process(COMMAND "${NODE}" --check "${javascript}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0 OR NOT error STREQUAL "")
  message(FATAL_ERROR "mutable JavaScript syntax failed (${status}): ${output}${error}")
endif()
execute_process(COMMAND "${NODE}" "${javascript}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${TEST_BINARY_DIR}/javascript.stdout.txt" "${output}")
file(WRITE "${TEST_BINARY_DIR}/javascript.stderr.txt" "${error}")
if(NOT status EQUAL 0 OR NOT error STREQUAL "")
  message(FATAL_ERROR "mutable JavaScript execution failed (${status}): ${output}${error}")
endif()
string(REGEX REPLACE "[ \t\r\n]+" " " normalized "${output}")
string(STRIP "${normalized}" normalized)
if(NOT normalized STREQUAL "2 1 1 2 3")
  message(FATAL_ERROR "mutable JavaScript values differ: '${normalized}'")
endif()
file(READ "${source_map}" map_json)
string(JSON map_version GET "${map_json}" version)
string(JSON map_source GET "${map_json}" sources 0)
string(JSON mappings GET "${map_json}" mappings)
if(NOT map_version EQUAL 3 OR NOT map_source STREQUAL INPUT OR mappings STREQUAL "")
  message(FATAL_ERROR "mutable JavaScript source map lost its source/mappings")
endif()

# The same public MIR must reach the independent C++ capability boundary.
execute_process(
  COMMAND "${MPFC}" --target cpp --no-banner --diagnostics-format json
          "${INPUT}" -o "${TEST_BINARY_DIR}/rejected.cpp"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${TEST_BINARY_DIR}/cpp-diagnostics.json" "${error}")
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
   EXISTS "${TEST_BINARY_DIR}/rejected.cpp")
  message(FATAL_ERROR "mutable C++ must fail without publishing code (${status}): ${output}${error}")
endif()
string(JSON count LENGTH "${error}" diagnostics)
set(capability_failure FALSE)
if(count LESS 1)
  message(FATAL_ERROR "mutable C++ rejection has no diagnostic")
endif()
math(EXPR last "${count} - 1")
foreach(index RANGE 0 ${last})
  string(JSON code GET "${error}" diagnostics ${index} code)
  if(code STREQUAL "MPF0006")
    message(FATAL_ERROR "mutable source was incorrectly rejected by shared MIR: ${error}")
  elseif(code STREQUAL "MPF2007")
    set(capability_failure TRUE)
  endif()
endforeach()
if(NOT capability_failure)
  message(FATAL_ERROR "mutable C++ rejection lost its target capability diagnostic: ${error}")
endif()
file(WRITE "${TEST_BINARY_DIR}/capability-result.txt"
  "case=matlab-mutable-value-joins\ninput=${INPUT}\nnode=${NODE}\n"
  "javascript=${normalized}\ncpp=MPF2007\n")
message(STATUS "mutable JavaScript values/source map and independent C++ rejection verified")
