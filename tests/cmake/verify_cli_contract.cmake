if(NOT DEFINED MPFC OR NOT DEFINED TEST_SOURCE_DIR OR NOT DEFINED TEST_BINARY_DIR OR
   NOT DEFINED SCHEMA)
  message(FATAL_ERROR "MPFC, TEST_SOURCE_DIR, TEST_BINARY_DIR and SCHEMA are required")
endif()

file(READ "${SCHEMA}" diagnostic_schema)
string(JSON schema_version GET "${diagnostic_schema}" properties schemaVersion const)
if(NOT schema_version EQUAL 1)
  message(FATAL_ERROR "diagnostic schema does not declare version one")
endif()

set(error_input "${TEST_SOURCE_DIR}/fixtures/diagnostic_error.py")
set(valid_input "${PROJECT_SOURCE_DIR}/examples/python/basic.py")
set(fixed_fortran_input "${PROJECT_SOURCE_DIR}/examples/fortran/fixed_form.f")

execute_process(
  COMMAND "${MPFC}" --diagnostics-format json --unknown
  RESULT_VARIABLE command_status
  OUTPUT_VARIABLE command_output
  ERROR_VARIABLE command_error)
if(NOT command_status EQUAL 2 OR NOT command_output STREQUAL "")
  message(FATAL_ERROR "command-line error contract failed: status=${command_status}")
endif()
string(JSON command_schema GET "${command_error}" schemaVersion)
string(JSON command_code GET "${command_error}" diagnostics 0 code)
if(NOT command_schema EQUAL 1 OR NOT command_code STREQUAL "MPFCLI0001")
  message(FATAL_ERROR "command-line JSON diagnostic contract failed: ${command_error}")
endif()

execute_process(
  COMMAND "${MPFC}" --diagnostics-format json --language python "${error_input}"
  RESULT_VARIABLE compilation_status
  OUTPUT_VARIABLE compilation_output
  ERROR_VARIABLE compilation_error)
if(NOT compilation_status EQUAL 1 OR NOT compilation_output STREQUAL "")
  message(FATAL_ERROR "compilation error contract failed: status=${compilation_status}")
endif()
string(JSON compilation_schema GET "${compilation_error}" schemaVersion)
string(JSON compilation_code GET "${compilation_error}" diagnostics 0 code)
string(JSON compilation_source GET "${compilation_error}" diagnostics 0 source)
if(NOT compilation_schema EQUAL 1 OR NOT compilation_code STREQUAL "MPF2001" OR
   NOT compilation_source STREQUAL "${error_input}")
  message(FATAL_ERROR "compilation JSON diagnostic contract failed: ${compilation_error}")
endif()

execute_process(
  COMMAND "${MPFC}" --language python "${error_input}"
  RESULT_VARIABLE text_status
  OUTPUT_VARIABLE text_output
  ERROR_VARIABLE text_error)
if(NOT text_status EQUAL 1 OR NOT text_output STREQUAL "")
  message(FATAL_ERROR "text diagnostic status contract failed: status=${text_status}")
endif()
string(FIND "${text_error}" "1 | print(missing)" snippet_position)
string(FIND "${text_error}" "| ^" caret_position)
if(snippet_position EQUAL -1 OR caret_position EQUAL -1)
  message(FATAL_ERROR "text diagnostic source rendering failed: ${text_error}")
endif()

set(missing_input "${TEST_BINARY_DIR}/definitely-missing-input.py")
execute_process(
  COMMAND "${MPFC}" --diagnostics-format json "${missing_input}"
  RESULT_VARIABLE input_status
  OUTPUT_VARIABLE input_output
  ERROR_VARIABLE input_error)
if(NOT input_status EQUAL 3 OR NOT input_output STREQUAL "")
  message(FATAL_ERROR "input error contract failed: status=${input_status}")
endif()
string(JSON input_code GET "${input_error}" diagnostics 0 code)
if(NOT input_code STREQUAL "MPFCLI0002")
  message(FATAL_ERROR "input JSON diagnostic contract failed: ${input_error}")
endif()

execute_process(
  COMMAND "${MPFC}" --diagnostics-format json --language python
          --output "${TEST_BINARY_DIR}" "${valid_input}"
  RESULT_VARIABLE output_status
  OUTPUT_VARIABLE output_output
  ERROR_VARIABLE output_error)
if(NOT output_status EQUAL 4 OR NOT output_output STREQUAL "")
  message(FATAL_ERROR "output error contract failed: status=${output_status}")
endif()
string(JSON output_code GET "${output_error}" diagnostics 0 code)
if(NOT output_code STREQUAL "MPFCLI0003")
  message(FATAL_ERROR "output JSON diagnostic contract failed: ${output_error}")
endif()

execute_process(
  COMMAND "${MPFC}" --diagnostics-format json --language python --no-banner "${valid_input}"
  RESULT_VARIABLE success_status
  OUTPUT_VARIABLE success_output
  ERROR_VARIABLE success_error)
if(NOT success_status EQUAL 0)
  message(FATAL_ERROR "success status contract failed: status=${success_status}")
endif()
string(JSON success_count LENGTH "${success_error}" diagnostics)
if(NOT success_count EQUAL 0 OR success_output STREQUAL "")
  message(FATAL_ERROR "success JSON diagnostic contract failed")
endif()

set(source_map_output "${TEST_BINARY_DIR}/cli-contract.js")
set(source_map_path "${TEST_BINARY_DIR}/cli-contract.js.map")
file(REMOVE "${source_map_output}" "${source_map_path}")
execute_process(
  COMMAND "${MPFC}" --language python --no-banner --output "${source_map_output}"
          --source-map "${source_map_path}" "${valid_input}"
  RESULT_VARIABLE source_map_status
  OUTPUT_VARIABLE source_map_stdout
  ERROR_VARIABLE source_map_stderr)
if(NOT source_map_status EQUAL 0 OR NOT source_map_stdout STREQUAL "" OR
   NOT source_map_stderr STREQUAL "" OR NOT EXISTS "${source_map_path}")
  message(FATAL_ERROR "source map CLI contract failed: ${source_map_stderr}")
endif()
file(READ "${source_map_path}" source_map_json)
string(JSON source_map_version GET "${source_map_json}" version)
string(JSON source_map_source GET "${source_map_json}" sources 0)
if(NOT source_map_version EQUAL 3 OR NOT source_map_source STREQUAL "${valid_input}")
  message(FATAL_ERROR "source map v3 payload is invalid: ${source_map_json}")
endif()

execute_process(
  COMMAND "${MPFC}" --fortran-form fixed --target javascript --no-banner -
  INPUT_FILE "${fixed_fortran_input}"
  RESULT_VARIABLE fixed_form_status
  OUTPUT_VARIABLE fixed_form_output
  ERROR_VARIABLE fixed_form_error)
if(NOT fixed_form_status EQUAL 0 OR fixed_form_output STREQUAL "" OR
   NOT fixed_form_error STREQUAL "")
  message(FATAL_ERROR
    "explicit fixed-form stdin contract failed (${fixed_form_status}): ${fixed_form_error}")
endif()

execute_process(
  COMMAND "${MPFC}" --diagnostics-format json --fortran-form invalid "${fixed_fortran_input}"
  RESULT_VARIABLE invalid_form_status
  OUTPUT_VARIABLE invalid_form_output
  ERROR_VARIABLE invalid_form_error)
if(NOT invalid_form_status EQUAL 2 OR NOT invalid_form_output STREQUAL "")
  message(FATAL_ERROR "invalid Fortran form status contract failed")
endif()
string(JSON invalid_form_code GET "${invalid_form_error}" diagnostics 0 code)
if(NOT invalid_form_code STREQUAL "MPFCLI0001")
  message(FATAL_ERROR "invalid Fortran form diagnostic contract failed: ${invalid_form_error}")
endif()
