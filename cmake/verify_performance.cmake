if(NOT DEFINED BENCHMARK OR NOT DEFINED BASELINE OR NOT DEFINED REPORT)
  message(FATAL_ERROR "BENCHMARK, BASELINE and REPORT are required")
endif()

execute_process(
  COMMAND "${BENCHMARK}"
  RESULT_VARIABLE benchmark_status
  OUTPUT_VARIABLE benchmark_output
  ERROR_VARIABLE benchmark_error
  TIMEOUT 120)
if(NOT benchmark_status EQUAL 0)
  message(FATAL_ERROR "performance benchmark failed: ${benchmark_error}")
endif()

file(READ "${BASELINE}" baseline_json)
string(JSON baseline_schema GET "${baseline_json}" schemaVersion)
string(JSON report_schema GET "${benchmark_output}" schemaVersion)
if(NOT baseline_schema EQUAL 1 OR NOT report_schema EQUAL 1)
  message(FATAL_ERROR "performance report/baseline schema mismatch")
endif()

foreach(metric IN ITEMS maxLatencyNanoseconds maxPeakArenaBytes maxGeneratedBytes)
  string(JSON actual GET "${benchmark_output}" "${metric}")
  string(JSON limit GET "${baseline_json}" "${metric}")
  if(actual GREATER limit)
    message(FATAL_ERROR "performance regression: ${metric}=${actual}, limit=${limit}")
  endif()
endforeach()

string(JSON actual_throughput GET "${benchmark_output}" minThroughputBytesPerSecond)
string(JSON minimum_throughput GET "${baseline_json}" minThroughputBytesPerSecond)
if(actual_throughput LESS minimum_throughput)
  message(FATAL_ERROR
    "performance regression: throughput=${actual_throughput}, minimum=${minimum_throughput}")
endif()

file(WRITE "${REPORT}" "${benchmark_output}")
message(STATUS "MPF performance release gate passed: ${benchmark_output}")
