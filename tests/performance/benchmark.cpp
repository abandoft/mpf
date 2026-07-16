#include <algorithm>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "mpf/transpiler.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Scenario {
  std::string name;
  std::string source;
  mpf::SourceLanguage language{mpf::SourceLanguage::python};
};

struct Measurement {
  std::string name;
  std::size_t latency_nanoseconds{0};
  std::size_t throughput_bytes_per_second{0};
  std::size_t peak_arena_bytes{0};
  std::size_t generated_bytes{0};
};

std::string assignment_workload(const std::size_t statements) {
  std::string source = "value = 0\n";
  for (std::size_t index = 0; index < statements; ++index) source += "value = value + 1\n";
  source += "print(value)\n";
  return source;
}

std::string control_flow_workload(const std::size_t branches) {
  std::string source = "value = 0\n";
  for (std::size_t index = 0; index < branches; ++index) {
    source += "if value < " + std::to_string(index + 1U) + ":\n";
    source += "    value = value + 1\n";
    source += "else:\n";
    source += "    value = value - 1\n";
  }
  source += "print(value)\n";
  return source;
}

std::string shape_workload(const std::size_t width) {
  std::string source = "matrix = [";
  for (std::size_t row = 0; row < width; ++row) {
    if (row != 0) source += ", ";
    source += '[';
    for (std::size_t column = 0; column < width; ++column) {
      if (column != 0) source += ", ";
      source += std::to_string(row * width + column);
    }
    source += ']';
  }
  source += "]\nprint(sum(matrix[0]))\n";
  return source;
}

std::string function_workload(const std::size_t functions) {
  std::string source;
  for (std::size_t index = 0; index < functions; ++index) {
    source += "def function_" + std::to_string(index) + "(value):\n";
    source += "    return value + " + std::to_string(index) + "\n";
  }
  source += "print(function_0(1))\n";
  return source;
}

mpf::TranspileResult compile(const Scenario& scenario, const mpf::TargetLanguage target) {
  mpf::TranspileOptions options;
  options.language = scenario.language;
  options.target = target;
  options.filename = scenario.name + ".py";
  options.emit_source_banner = false;
  return mpf::Transpiler{}.transpile(scenario.source, options);
}

bool measure(const Scenario& scenario, Measurement& measurement) {
  std::vector<std::size_t> samples;
  std::size_t peak_arena = 0;
  std::size_t generated = 0;
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto reference = compile(scenario, target);
    if (!reference.success()) return false;
    for (std::size_t sample = 0; sample < 3; ++sample) {
      const auto started = Clock::now();
      const auto result = compile(scenario, target);
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started);
      if (!result.success() || result.code != reference.code ||
          result.source_map.to_json() != reference.source_map.to_json() ||
          result.dependencies != reference.dependencies) {
        return false;
      }
      samples.push_back(static_cast<std::size_t>(elapsed.count()));
      peak_arena = std::max(peak_arena, result.report.peak_arena_bytes);
      generated = std::max(generated, result.code.size());
    }
  }
  std::sort(samples.begin(), samples.end());
  measurement.name = scenario.name;
  measurement.latency_nanoseconds = samples[samples.size() / 2U];
  measurement.throughput_bytes_per_second =
      measurement.latency_nanoseconds == 0
          ? 0
          : scenario.source.size() * 1'000'000'000U / measurement.latency_nanoseconds;
  measurement.peak_arena_bytes = peak_arena;
  measurement.generated_bytes = generated;
  return true;
}

bool concurrent_gate(const Scenario& scenario) {
  std::vector<std::future<mpf::TranspileResult>> work;
  work.reserve(8);
  for (std::size_t index = 0; index < 8; ++index) {
    work.push_back(std::async(std::launch::async, [&, index] {
      return compile(scenario,
                     index % 2U == 0 ? mpf::TargetLanguage::javascript : mpf::TargetLanguage::cpp);
    }));
  }
  for (auto& future : work) {
    if (!future.get().success()) return false;
  }
  return true;
}

}  // namespace

int main() {
  const std::vector<Scenario> scenarios{{"small", "value = 40 + 2\nprint(value)\n"},
                                        {"throughput", assignment_workload(600)},
                                        {"cfg", control_flow_workload(80)},
                                        {"shape", shape_workload(16)},
                                        {"function-graph", function_workload(40)}};
  std::vector<Measurement> measurements;
  for (const auto& scenario : scenarios) {
    Measurement measurement;
    if (!measure(scenario, measurement)) {
      std::cerr << "performance scenario failed: " << scenario.name << '\n';
      return 1;
    }
    measurements.push_back(std::move(measurement));
  }
  if (!concurrent_gate(scenarios.front())) {
    std::cerr << "parallel compilation gate failed\n";
    return 1;
  }

  std::size_t max_latency = 0;
  std::size_t min_throughput = static_cast<std::size_t>(-1);
  std::size_t max_arena = 0;
  std::size_t max_generated = 0;
  for (const auto& measurement : measurements) {
    max_latency = std::max(max_latency, measurement.latency_nanoseconds);
    min_throughput = std::min(min_throughput, measurement.throughput_bytes_per_second);
    max_arena = std::max(max_arena, measurement.peak_arena_bytes);
    max_generated = std::max(max_generated, measurement.generated_bytes);
  }

  std::cout << "{\"schemaVersion\":1,\"maxLatencyNanoseconds\":" << max_latency
            << ",\"minThroughputBytesPerSecond\":" << min_throughput
            << ",\"maxPeakArenaBytes\":" << max_arena << ",\"maxGeneratedBytes\":" << max_generated
            << ",\"parallelCompilations\":8,\"scenarios\":[";
  for (std::size_t index = 0; index < measurements.size(); ++index) {
    if (index != 0) std::cout << ',';
    const auto& value = measurements[index];
    std::cout << "{\"name\":\"" << value.name
              << "\",\"latencyNanoseconds\":" << value.latency_nanoseconds
              << ",\"throughputBytesPerSecond\":" << value.throughput_bytes_per_second
              << ",\"peakArenaBytes\":" << value.peak_arena_bytes
              << ",\"generatedBytes\":" << value.generated_bytes << '}';
  }
  std::cout << "]}\n";
  return 0;
}
