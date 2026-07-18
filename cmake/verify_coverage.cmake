cmake_minimum_required(VERSION 3.20)

foreach(required PROFILE_DIR OUTPUT_DIR LLVM_PROFDATA LLVM_COV PRIMARY_OBJECT MIN_LINE_PERCENT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "verify_coverage.cmake requires ${required}")
  endif()
endforeach()

file(GLOB profiles "${PROFILE_DIR}/*.profraw")
if(NOT profiles)
  message(FATAL_ERROR "No raw coverage profiles were generated in ${PROFILE_DIR}")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(merged_profile "${OUTPUT_DIR}/mpf.profdata")
execute_process(
  COMMAND "${LLVM_PROFDATA}" merge -sparse ${profiles} -o "${merged_profile}"
  RESULT_VARIABLE merge_result
  OUTPUT_VARIABLE merge_output
  ERROR_VARIABLE merge_error)
if(NOT merge_result EQUAL 0)
  message(FATAL_ERROR "llvm-profdata failed:\n${merge_output}${merge_error}")
endif()

set(object_arguments)
foreach(optional_object EXTRA_OBJECT FUZZ_OBJECT BENCHMARK_OBJECT)
  if(DEFINED ${optional_object} AND EXISTS "${${optional_object}}")
    list(APPEND object_arguments "-object=${${optional_object}}")
  endif()
endforeach()
set(ignore_regex "(^|/)(build|tests)/")
execute_process(
  COMMAND "${LLVM_COV}" export "${PRIMARY_OBJECT}" ${object_arguments}
    "-instr-profile=${merged_profile}"
    "-ignore-filename-regex=${ignore_regex}"
    -summary-only
  RESULT_VARIABLE export_result
  OUTPUT_VARIABLE coverage_json
  ERROR_VARIABLE export_error)
if(NOT export_result EQUAL 0)
  message(FATAL_ERROR "llvm-cov export failed:\n${export_error}")
endif()
file(WRITE "${OUTPUT_DIR}/summary.json" "${coverage_json}")

string(JSON line_percent GET "${coverage_json}" data 0 totals lines percent)
string(JSON covered_lines GET "${coverage_json}" data 0 totals lines covered)
string(JSON total_lines GET "${coverage_json}" data 0 totals lines count)

execute_process(
  COMMAND "${LLVM_COV}" show "${PRIMARY_OBJECT}" ${object_arguments}
    "-instr-profile=${merged_profile}"
    "-ignore-filename-regex=${ignore_regex}"
    -format=html
    "-output-dir=${OUTPUT_DIR}/html"
  RESULT_VARIABLE show_result
  ERROR_VARIABLE show_error)
if(NOT show_result EQUAL 0)
  message(FATAL_ERROR "llvm-cov show failed:\n${show_error}")
endif()

message(STATUS
  "MPF production line coverage: ${line_percent}% (${covered_lines}/${total_lines}); "
  "required: ${MIN_LINE_PERCENT}%")
if(line_percent LESS MIN_LINE_PERCENT)
  message(FATAL_ERROR
    "MPF line coverage ${line_percent}% is below the ${MIN_LINE_PERCENT}% quality gate")
endif()
