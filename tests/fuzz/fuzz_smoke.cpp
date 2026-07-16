#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "mpf/transpiler.hpp"

namespace {

struct Seed {
  std::string source;
  mpf::SourceLanguage language{mpf::SourceLanguage::python};
};

mpf::SourceLanguage language_for_path(const std::filesystem::path& path) {
  const auto value = path.generic_string();
  if (value.find("matlab") != std::string::npos) return mpf::SourceLanguage::matlab;
  if (value.find("fortran") != std::string::npos) return mpf::SourceLanguage::fortran;
  if (value.find("typescript") != std::string::npos) return mpf::SourceLanguage::typescript;
  return mpf::SourceLanguage::python;
}

std::vector<Seed> load_seeds(const std::filesystem::path& root) {
  std::vector<Seed> result;
  if (!std::filesystem::exists(root)) return result;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;
    std::ifstream input(entry.path(), std::ios::binary);
    std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    result.push_back({std::move(source), language_for_path(entry.path())});
  }
  return result;
}

bool same_diagnostics(const std::vector<mpf::Diagnostic>& left,
                      const std::vector<mpf::Diagnostic>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].severity != right[index].severity || left[index].code != right[index].code ||
        left[index].message != right[index].message ||
        left[index].location.line != right[index].location.line ||
        left[index].location.column != right[index].location.column) {
      return false;
    }
  }
  return true;
}

bool deterministic(const std::string& source, const mpf::SourceLanguage language,
                   const mpf::TargetLanguage target) {
  mpf::TranspileOptions options;
  options.language = language;
  options.target = target;
  options.filename = "fuzz-input";
  options.emit_source_banner = false;
  options.resource_limits.max_source_bytes = 1024U * 1024U;
  options.resource_limits.max_tokens = 256U * 1024U;
  options.resource_limits.max_ast_nodes = 256U * 1024U;
  options.resource_limits.max_hir_nodes = 256U * 1024U;
  options.resource_limits.max_mir_instructions = 512U * 1024U;
  options.resource_limits.max_lir_nodes = 512U * 1024U;
  options.resource_limits.max_generated_bytes = 8U * 1024U * 1024U;
  const auto first = mpf::Transpiler{}.transpile(source, options);
  const auto second = mpf::Transpiler{}.transpile(source, options);
  return first.code == second.code && first.source_map.to_json() == second.source_map.to_json() &&
         first.dependencies == second.dependencies &&
         same_diagnostics(first.diagnostics, second.diagnostics);
}

std::vector<std::string> mutations(const std::string& source) {
  std::vector<std::string> result{source, source.substr(0, source.size() / 2U)};
  const auto mutations = std::min<std::size_t>(source.size(), 16U);
  for (std::size_t index = 0; index < mutations; ++index) {
    auto mutated = source;
    mutated[index] = static_cast<char>(static_cast<unsigned char>(mutated[index]) ^ 0x5aU);
    result.push_back(std::move(mutated));
  }
  result.emplace_back(4096U, '(');
  result.emplace_back(4096U, '\xff');
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  const auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{};
  auto seeds = load_seeds(root);
  if (seeds.empty()) {
    seeds.push_back({"value = [1, 2, 3]\nprint(value[0])\n", mpf::SourceLanguage::python});
  }
  std::size_t cases = 0;
  try {
    for (const auto& seed : seeds) {
      for (const auto& source : mutations(seed.source)) {
        for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
          ++cases;
          if (!deterministic(source, seed.language, target)) {
            std::cerr << "non-deterministic fuzz case " << cases << '\n';
            return 1;
          }
        }
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "uncaught fuzz exception: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "uncaught non-standard fuzz exception\n";
    return 1;
  }
  std::cout << "fuzz-smoke cases=" << cases << '\n';
  return 0;
}
