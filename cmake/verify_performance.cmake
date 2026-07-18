cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED BENCHMARK OR NOT DEFINED BASELINE OR NOT DEFINED REPORT OR
   NOT DEFINED PROJECT_VERSION)
  message(FATAL_ERROR "BENCHMARK, BASELINE, REPORT and PROJECT_VERSION are required")
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
if(NOT baseline_schema EQUAL 2 OR NOT report_schema EQUAL 2)
  message(FATAL_ERROR "performance report/baseline schema mismatch")
endif()
string(JSON baseline_version GET "${baseline_json}" projectVersion)
string(JSON report_version GET "${benchmark_output}" projectVersion)
if(NOT baseline_version STREQUAL PROJECT_VERSION OR NOT report_version STREQUAL PROJECT_VERSION)
  message(FATAL_ERROR
    "performance baseline version mismatch: project=${PROJECT_VERSION}, "
    "baseline=${baseline_version}, report=${report_version}")
endif()

foreach(metric IN ITEMS maxLatencyNanoseconds maxPeakArenaBytes maxGeneratedBytes)
  string(JSON actual GET "${benchmark_output}" "${metric}")
  string(JSON limit GET "${baseline_json}" "${metric}")
  if(actual GREATER limit)
    message(FATAL_ERROR "performance regression: ${metric}=${actual}, limit=${limit}")
  endif()
endforeach()

foreach(metric IN ITEMS matlabMaxLatencyNanoseconds matlabMaxGeneratedBytes)
  string(JSON actual GET "${benchmark_output}" "${metric}")
  string(JSON limit GET "${baseline_json}" "${metric}")
  if(actual GREATER limit)
    message(FATAL_ERROR "Matlab performance regression: ${metric}=${actual}, limit=${limit}")
  endif()
endforeach()

string(JSON actual_throughput GET "${benchmark_output}" minThroughputBytesPerSecond)
string(JSON minimum_throughput GET "${baseline_json}" minThroughputBytesPerSecond)
if(actual_throughput LESS minimum_throughput)
  message(FATAL_ERROR
    "performance regression: throughput=${actual_throughput}, minimum=${minimum_throughput}")
endif()

string(JSON matlab_throughput GET "${benchmark_output}" matlabMinThroughputBytesPerSecond)
string(JSON matlab_minimum_throughput GET "${baseline_json}"
  matlabMinThroughputBytesPerSecond)
if(matlab_throughput LESS matlab_minimum_throughput)
  message(FATAL_ERROR
    "Matlab performance regression: throughput=${matlab_throughput}, "
    "minimum=${matlab_minimum_throughput}")
endif()

file(WRITE "${REPORT}" "${benchmark_output}")
message(STATUS "MPF performance release gate passed: ${benchmark_output}")
