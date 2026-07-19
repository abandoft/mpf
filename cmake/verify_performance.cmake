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
if(NOT baseline_schema EQUAL 3 OR NOT report_schema EQUAL 3)
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

string(JSON default_min_throughput GET "${baseline_json}" minThroughputBytesPerSecond)
string(JSON matlab_min_throughput GET "${baseline_json}" matlabMinThroughputBytesPerSecond)
string(JSON default_max_latency GET "${baseline_json}" maxLatencyNanoseconds)
string(JSON matlab_max_latency GET "${baseline_json}" matlabMaxLatencyNanoseconds)
string(JSON default_max_arena GET "${baseline_json}" maxPeakArenaBytes)
string(JSON default_max_generated GET "${baseline_json}" maxGeneratedBytes)
string(JSON matlab_max_generated GET "${baseline_json}" matlabMaxGeneratedBytes)
string(JSON override_count LENGTH "${baseline_json}" scenarioLimits)
string(JSON scenario_count LENGTH "${benchmark_output}" scenarios)
if(scenario_count EQUAL 0)
  message(FATAL_ERROR "performance report contains no scenarios")
endif()

set(seen_overrides)
math(EXPR last_scenario "${scenario_count} - 1")
foreach(index RANGE 0 ${last_scenario})
  string(JSON name GET "${benchmark_output}" scenarios ${index} name)
  string(JSON latency GET "${benchmark_output}" scenarios ${index} latencyNanoseconds)
  string(JSON throughput GET "${benchmark_output}" scenarios ${index}
    throughputBytesPerSecond)
  string(JSON arena GET "${benchmark_output}" scenarios ${index} peakArenaBytes)
  string(JSON generated GET "${benchmark_output}" scenarios ${index} generatedBytes)

  set(max_latency "${default_max_latency}")
  set(min_throughput "${default_min_throughput}")
  set(max_arena "${default_max_arena}")
  set(max_generated "${default_max_generated}")
  if(name MATCHES "^matlab-")
    set(max_latency "${matlab_max_latency}")
    set(min_throughput "${matlab_min_throughput}")
    set(max_generated "${matlab_max_generated}")
  endif()

  if(override_count GREATER 0)
    math(EXPR last_override "${override_count} - 1")
    foreach(override_index RANGE 0 ${last_override})
      string(JSON override_name MEMBER "${baseline_json}" scenarioLimits ${override_index})
      if(override_name STREQUAL name)
        list(APPEND seen_overrides "${name}")
        string(JSON max_latency GET "${baseline_json}" scenarioLimits "${name}"
          maxLatencyNanoseconds)
        string(JSON min_throughput GET "${baseline_json}" scenarioLimits "${name}"
          minThroughputBytesPerSecond)
        string(JSON max_arena GET "${baseline_json}" scenarioLimits "${name}"
          maxPeakArenaBytes)
        string(JSON max_generated GET "${baseline_json}" scenarioLimits "${name}"
          maxGeneratedBytes)
        break()
      endif()
    endforeach()
  endif()

  if(latency GREATER max_latency OR throughput LESS min_throughput OR
     arena GREATER max_arena OR generated GREATER max_generated)
    message(FATAL_ERROR
      "performance scenario regression: ${name}: latency=${latency}/${max_latency}, "
      "throughput=${throughput}/${min_throughput}, arena=${arena}/${max_arena}, "
      "generated=${generated}/${max_generated}")
  endif()
endforeach()

if(override_count GREATER 0)
  foreach(override_index RANGE 0 ${last_override})
    string(JSON override_name MEMBER "${baseline_json}" scenarioLimits ${override_index})
    list(FIND seen_overrides "${override_name}" seen_index)
    if(seen_index EQUAL -1)
      message(FATAL_ERROR
        "performance baseline contains an unknown scenario override: ${override_name}")
    endif()
  endforeach()
endif()

file(WRITE "${REPORT}" "${benchmark_output}")
message(STATUS "MPF performance release gate passed: ${benchmark_output}")
