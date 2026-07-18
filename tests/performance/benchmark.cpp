#include <algorithm>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "mpf/transpiler.hpp"
#include "mpf/version.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Scenario {
  std::string name;
  std::string source;
  mpf::SourceLanguage language{mpf::SourceLanguage::python};
  std::size_t minimum_memory_dependences{0};
  bool require_loop_carried_dependence{false};
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

std::string typescript_workload(const std::size_t statements) {
  std::string source = "let value: number = 0;\n";
  for (std::size_t index = 0; index < statements; ++index) source += "value = value + 1;\n";
  source += "console.log(value);\n";
  return source;
}

std::string storage_region_workload(const std::size_t calls) {
  std::string source =
      "program storage_regions\n"
      "integer :: values(1024)\n";
  for (std::size_t index = 0; index < calls; ++index) {
    source += "call update(values(1:1024:2), values(2:1024:2))\n";
  }
  source +=
      "print *, values(1), values(2)\n"
      "contains\n"
      "subroutine update(odd, even)\n"
      "integer, intent(out) :: odd(:), even(:)\n"
      "odd(1) = 40\n"
      "even(1) = 2\n"
      "end subroutine update\n"
      "end program storage_regions\n";
  return source;
}

std::string memory_dependence_workload(const std::size_t loops) {
  std::string source = "value = 0\n";
  for (std::size_t index = 0; index < loops; ++index) {
    source += "while value < " + std::to_string(index + 1U) + ":\n";
    source += "    if value < " + std::to_string(index) + ":\n";
    source += "        value = value + 2\n";
    source += "    else:\n";
    source += "        value = value + 1\n";
  }
  source += "print(value)\n";
  return source;
}

std::string matlab_array_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "matrix = [";
  for (std::size_t row = 0; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += std::to_string(row * width + column + 1U);
    }
  }
  source += "];\nrow = [";
  for (std::size_t column = 0; column < width; ++column) {
    if (column != 0U) source += ' ';
    source += std::to_string(column + 1U);
  }
  source += "];\ncolumn = [";
  for (std::size_t row = 0; row < width; ++row) {
    if (row != 0U) source += "; ";
    source += std::to_string(row + 1U);
  }
  source += "];\n";
  source += "row_mask = [";
  for (std::size_t row = 0; row < width; ++row) {
    if (row != 0U) source += ' ';
    source += row % 2U == 0U ? "true" : "false";
  }
  source += "];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "matrix = matrix + row;\n";
    source += "matrix = matrix + column;\n";
  }
  source +=
      "transposed = matrix.';\n"
      "selected = transposed(transposed > row);\n";
  source += "permuted = matrix(row_mask, [" + std::to_string(width) + " 1 1]);\n";
  source += "matrix(row_mask, [1 " + std::to_string(width) + "]) = 0;\n";
  source += "disp(sum(selected))\n";
  return source;
}

std::string matlab_tensor_workload(const std::size_t pages) {
  std::string source = "values = [";
  const auto elements = pages * 16U;
  for (std::size_t index = 0; index < elements; ++index) {
    if (index != 0U) source += ' ';
    source += std::to_string(index + 1U);
  }
  source += "];\ntensor = reshape(values, 4, 4, " + std::to_string(pages) + ");\n";
  source += "offsets = reshape([";
  for (std::size_t page = 0; page < pages; ++page) {
    if (page != 0U) source += ' ';
    source += std::to_string(page + 1U);
  }
  source += "], 1, 1, " + std::to_string(pages) + ");\n";
  source += "result = tensor + offsets;\ndisp(result(end, end, end))\n";
  return source;
}

std::string matlab_logical_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "row = [";
  for (std::size_t index = 0; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += index % 3U == 0U ? "0" : std::to_string(index + 1U);
  }
  source += "];\ncolumn = [";
  for (std::size_t index = 0; index < width; ++index) {
    if (index != 0U) source += "; ";
    source += index % 4U == 0U ? "0" : std::to_string(index + 1U);
  }
  source += "];\nmask = row & column;\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "mask = (mask | row) & ~column;\n";
  }
  source +=
      "condition = 0;\n"
      "if mask\n"
      "  condition = 1;\n"
      "end\n"
      "disp(condition)\n";
  return source;
}

std::string matlab_logical_reduction_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "matrix = [";
  for (std::size_t row = 0; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += (row + column) % 7U == 0U ? "0" : "1";
    }
  }
  source += "];\nvalues = [";
  for (std::size_t index = 0; index < width * 8U; ++index) {
    if (index != 0U) source += ' ';
    source += index % 11U == 0U ? "0" : "1";
  }
  source += "];\ntensor = reshape(values, 4, 2, " + std::to_string(width) + ");\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "column_all = all(matrix);\n";
    source += "row_any = any(matrix, 2);\n";
    source += "page_all = all(tensor, [1 3]);\n";
    source += "total_any = any(tensor, 'all');\n";
  }
  source +=
      "disp(total_any || any(column_all, 'all') || any(row_any, 'all') || "
      "any(page_all, 'all'))\n";
  return source;
}

std::string matlab_dynamic_end_workload(const std::size_t rounds) {
  std::string source =
      "values = [10 20 30 40 50 60 70 80];\n"
      "matrix = [1 2 3 4; 5 6 7 8; 9 10 11 12; 13 14 15 16];\n"
      "disp(dynamic_end_kernel(values, matrix))\n"
      "function result = dynamic_end_kernel(values, matrix)\n"
      "result = 0;\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "result = result + values(end) + values(end - 1) + sum(values(2:end)) + ";
    source += "matrix(end, end) + matrix(end) + sum(values([1 end]));\n";
  }
  source += "end\n";
  return source;
}

std::string matlab_dynamic_broadcast_workload(const std::size_t rounds) {
  std::string source =
      "column = [1; 2; 3; 4];\n"
      "row = [10 20 30 40];\n"
      "disp(dynamic_broadcast_kernel(column, row))\n"
      "function result = dynamic_broadcast_kernel(left, right)\n"
      "expanded = left + right;\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "expanded = expanded + right;\n";
    source += "mask = expanded > left;\n";
  }
  source += "result = sum(expanded) + sum(mask);\nend\n";
  return source;
}

std::string matlab_shape_mutation_workload(const std::size_t rounds) {
  std::string source =
      "values = [1 2 3 4 5 6 7 8];\n"
      "matrix = [1 2 3 4; 5 6 7 8; 9 10 11 12; 13 14 15 16];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "values(end + 1) = " + std::to_string(round + 1U) + ";\n";
    source += "values(2) = [];\n";
    source += "matrix(:, end + 1) = [1; 2; 3; 4];\n";
    source += "matrix(:, 2) = [];\n";
  }
  source += "disp(values(end) + matrix(end, end))\n";
  return source;
}

std::string matlab_empty_array_workload(const std::size_t rounds) {
  std::string source;
  for (std::size_t round = 0; round < rounds; ++round) {
    const auto suffix = std::to_string(round);
    source += "empty_" + suffix + " = reshape([], 0, 32);\n";
    source.append("scaled_").append(suffix).append(" = empty_").append(suffix).append(" + ");
    source.append(std::to_string(round + 1U)).append(";\n");
    source.append("transposed_").append(suffix).append(" = scaled_").append(suffix).append(".';\n");
    source.append("selected_")
        .append(suffix)
        .append(" = empty_")
        .append(suffix)
        .append("([], :);\n");
    source += "grown_" + suffix + " = [];\n";
    source += "grown_" + suffix + "(1, 32) = " + std::to_string(round + 1U) + ";\n";
  }
  const auto last = std::to_string(rounds - 1U);
  source += "disp(length(transposed_" + last + ") + length(selected_" + last + ") + grown_" + last +
            "(1, 32))\n";
  return source;
}

std::string matlab_matrix_solve_workload(const std::size_t rounds) {
  std::string source =
      "coefficient = [4 1 0 0; 2 5 1 0; 0 1 6 2; 0 0 2 7];\n"
      "right_hand_side = [9; 8; 7; 6];\n"
      "tall = [1 0 0; 0 1 0; 0 0 1; 1 1 0; 0 1 1];\n"
      "tall_rhs = [1 2; 2 3; 3 4; 4 5; 5 6];\n"
      "wide = [1 0 0 1 0; 0 1 0 0 1; 0 0 1 1 1];\n"
      "wide_rhs = [1 2; 2 3; 3 4];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "solution = coefficient \\ right_hand_side;\n";
    source += "quotient = [1 2 3 4] / coefficient;\n";
    source += "powered = coefficient ^ 3;\n";
    source += "least_squares = tall \\ tall_rhs;\n";
    source += "basic_underdetermined = wide \\ wide_rhs;\n";
    source += "rectangular_right = [1 2 3] / tall;\n";
  }
  source +=
      "disp(solution(1) + quotient(4) + powered(1, 1) + "
      "least_squares(1, 1) + basic_underdetermined(1, 1) + rectangular_right(1))\n";
  return source;
}

std::string matlab_rank_aware_solve_workload(const std::size_t rounds) {
  std::string source =
      "rank_deficient_tall = [1 2; 2 4; 3 6; 4 8];\n"
      "rank_deficient_rhs = [1 2; 2 4; 3 6; 4 8];\n"
      "wide = [1 0 1 2; 0 1 1 1];\n"
      "wide_rhs = [2 4; 3 6];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "ranked_basic = rank_deficient_tall \\ rank_deficient_rhs;\n";
    source += "wide_basic = wide \\ wide_rhs;\n";
    source += "ranked_right = [1 2] / rank_deficient_tall;\n";
  }
  source += "disp(ranked_basic(2, 1) + wide_basic(3, 2) + ranked_right(1))\n";
  return source;
}

std::string matlab_conditioned_square_solve_workload(const std::size_t rounds) {
  std::string source =
      "singular = [1 0; 0 0];\n"
      "nearly_singular = [16 2 3 13; 5 11 10 8; 9 7 6 12; 4 14 15 1];\n"
      "small_rhs = [1; 1];\n"
      "large_rhs = [34; 34; 34; 34];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "singular_left = singular \\ small_rhs;\n";
    source += "singular_right = [1 1] / singular;\n";
    source += "conditioned = nearly_singular \\ large_rhs;\n";
  }
  source += "disp(singular_left(1) + singular_right(1) + conditioned(1))\n";
  return source;
}

std::string matlab_structured_square_solve_workload(const std::size_t rounds) {
  std::string source =
      "diagonal = [4 0 0 0; 0 5 0 0; 0 0 6 0; 0 0 0 7.5];\n"
      "lower = [4 0 0 0; 1 5 0 0; 2 1 6 0; 3 2 1 7.5];\n"
      "upper = [4 1 2 3; 0 5 1 2; 0 0 6 1; 0 0 0 7.5];\n"
      "dense = [8 1 2 3; 1 9 3 2; 2 3 10 1; 3 2 1 11.5];\n"
      "right_hand_side = [4; 6; 8; 10];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "diagonal_left = diagonal \\ right_hand_side;\n";
    source += "lower_left = lower \\ right_hand_side;\n";
    source += "upper_left = upper \\ right_hand_side;\n";
    source += "dense_left = dense \\ right_hand_side;\n";
    source += "upper_right = [1 2 3 4] / upper;\n";
  }
  source +=
      "disp(diagonal_left(1) + lower_left(2) + upper_left(3) + dense_left(4) + "
      "upper_right(1))\n";
  return source;
}

std::string matlab_advanced_structured_square_solve_workload(const std::size_t rounds) {
  std::string source =
      "tridiagonal = [0 2 0 0; 1 3 4 0; 0 5 6 7; 0 0 8 9];\n"
      "positive_definite = [8 1 2 3; 1 9 3 2; 2 3 10 1; 3 2 1 11.5];\n"
      "symmetric_indefinite = [0 1 2 3; 1 0 4 5; 2 4 0 6; 3 5 6 0];\n"
      "right_hand_side = [4; 6; 8; 10];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "tridiagonal_left = tridiagonal \\ right_hand_side;\n";
    source += "tridiagonal_right = [1 2 3 4] / tridiagonal;\n";
    source += "positive_definite_left = positive_definite \\ right_hand_side;\n";
    source += "positive_definite_right = [1 2 3 4] / positive_definite;\n";
    source += "indefinite_left = symmetric_indefinite \\ right_hand_side;\n";
  }
  source +=
      "disp(tridiagonal_left(1) + tridiagonal_right(2) + positive_definite_left(3) + "
      "positive_definite_right(4) + indefinite_left(1))\n";
  return source;
}

std::string source_extension(const mpf::SourceLanguage language) {
  switch (language) {
    case mpf::SourceLanguage::python: return ".py";
    case mpf::SourceLanguage::matlab: return ".m";
    case mpf::SourceLanguage::fortran: return ".f90";
    case mpf::SourceLanguage::typescript: return ".ts";
    case mpf::SourceLanguage::automatic: return ".txt";
  }
  return ".txt";
}

mpf::TranspileResult compile(const Scenario& scenario, const mpf::TargetLanguage target) {
  mpf::TranspileOptions options;
  options.language = scenario.language;
  options.target = target;
  options.filename = scenario.name + source_extension(scenario.language);
  options.emit_source_banner = false;
  return mpf::Transpiler{}.transpile(scenario.source, options);
}

bool measure(const Scenario& scenario, Measurement& measurement) {
  std::vector<std::size_t> samples;
  std::size_t peak_arena = 0;
  std::size_t generated = 0;
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto reference = compile(scenario, target);
    if (!reference.success() ||
        reference.report.mir_memory_dependences.total < scenario.minimum_memory_dependences ||
        (scenario.require_loop_carried_dependence &&
         reference.report.mir_memory_dependences.loop_carried == 0U)) {
      return false;
    }
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
  const std::vector<Scenario> scenarios{
      {"small", "value = 40 + 2\nprint(value)\n"},
      {"throughput", assignment_workload(600)},
      {"cfg", control_flow_workload(80)},
      {"shape", shape_workload(16)},
      {"function-graph", function_workload(40)},
      {"typescript-throughput", typescript_workload(300), mpf::SourceLanguage::typescript},
      {"storage-regions", storage_region_workload(128), mpf::SourceLanguage::fortran},
      {"memory-dependence", memory_dependence_workload(16), mpf::SourceLanguage::python, 32U, true},
      {"matlab-array-kernel", matlab_array_workload(24, 24), mpf::SourceLanguage::matlab},
      {"matlab-tensor-kernel", matlab_tensor_workload(24), mpf::SourceLanguage::matlab},
      {"matlab-logical-kernel", matlab_logical_workload(24, 24), mpf::SourceLanguage::matlab},
      {"matlab-logical-reduction", matlab_logical_reduction_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-dynamic-end", matlab_dynamic_end_workload(32), mpf::SourceLanguage::matlab},
      {"matlab-dynamic-broadcast", matlab_dynamic_broadcast_workload(32),
       mpf::SourceLanguage::matlab},
      {"matlab-shape-mutation", matlab_shape_mutation_workload(32), mpf::SourceLanguage::matlab},
      {"matlab-empty-arrays", matlab_empty_array_workload(24), mpf::SourceLanguage::matlab},
      {"matlab-matrix-solve", matlab_matrix_solve_workload(24), mpf::SourceLanguage::matlab},
      {"matlab-rank-aware-solve", matlab_rank_aware_solve_workload(24),
       mpf::SourceLanguage::matlab},
      {"matlab-conditioned-square-solve", matlab_conditioned_square_solve_workload(24),
       mpf::SourceLanguage::matlab},
      {"matlab-structured-square-solve", matlab_structured_square_solve_workload(24),
       mpf::SourceLanguage::matlab},
      {"matlab-advanced-structured-square-solve",
       matlab_advanced_structured_square_solve_workload(24), mpf::SourceLanguage::matlab}};
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
  std::size_t matlab_max_latency = 0;
  std::size_t matlab_min_throughput = static_cast<std::size_t>(-1);
  std::size_t matlab_max_generated = 0;
  for (const auto& measurement : measurements) {
    max_latency = std::max(max_latency, measurement.latency_nanoseconds);
    min_throughput = std::min(min_throughput, measurement.throughput_bytes_per_second);
    max_arena = std::max(max_arena, measurement.peak_arena_bytes);
    max_generated = std::max(max_generated, measurement.generated_bytes);
    if (measurement.name.rfind("matlab-", 0U) == 0U) {
      matlab_max_latency = std::max(matlab_max_latency, measurement.latency_nanoseconds);
      matlab_min_throughput =
          std::min(matlab_min_throughput, measurement.throughput_bytes_per_second);
      matlab_max_generated = std::max(matlab_max_generated, measurement.generated_bytes);
    }
  }

  std::cout << "{\"schemaVersion\":2,\"projectVersion\":\"" << MPF_VERSION_STRING
            << "\",\"maxLatencyNanoseconds\":" << max_latency
            << ",\"minThroughputBytesPerSecond\":" << min_throughput
            << ",\"maxPeakArenaBytes\":" << max_arena << ",\"maxGeneratedBytes\":" << max_generated
            << ",\"matlabMaxLatencyNanoseconds\":" << matlab_max_latency
            << ",\"matlabMinThroughputBytesPerSecond\":" << matlab_min_throughput
            << ",\"matlabMaxGeneratedBytes\":" << matlab_max_generated
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
