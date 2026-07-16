#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "mpf/diagnostic.hpp"
#include "mpf/source_map.hpp"

namespace mpf {

enum class SourceLanguage { automatic, matlab, python, fortran };

enum class TargetLanguage { javascript, cpp };

enum class ModuleKind { script, esm };

enum class FortranSourceForm { automatic, free, fixed };

struct ResourceLimits {
  std::size_t max_source_bytes{64U * 1024U * 1024U};
  std::size_t max_tokens{16U * 1024U * 1024U};
  std::size_t max_parser_depth{1024U};
  std::size_t max_arena_bytes{512U * 1024U * 1024U};
  std::size_t max_ast_nodes{2U * 1024U * 1024U};
  std::size_t max_hir_nodes{2U * 1024U * 1024U};
  std::size_t max_mir_instructions{4U * 1024U * 1024U};
  std::size_t max_lir_nodes{4U * 1024U * 1024U};
  std::size_t max_generated_bytes{256U * 1024U * 1024U};
  std::size_t max_source_map_bytes{64U * 1024U * 1024U};
};

struct StageReport {
  std::string stage;
  std::size_t nodes{0};
  std::size_t duration_nanoseconds{0};
  std::size_t arena_bytes{0};
};

struct CompilationReport {
  std::size_t source_bytes{0};
  std::size_t total_nanoseconds{0};
  std::size_t peak_arena_bytes{0};
  std::vector<StageReport> stages;

  [[nodiscard]] std::string to_json() const;
};

struct TranspileOptions {
  SourceLanguage language{SourceLanguage::automatic};
  TargetLanguage target{TargetLanguage::javascript};
  ModuleKind module_kind{ModuleKind::esm};
  FortranSourceForm fortran_source_form{FortranSourceForm::automatic};
  ResourceLimits resource_limits{};
  std::string filename;
  std::string generated_filename;
  bool emit_source_banner{true};
  bool emit_source_map{true};
};

struct TranspileResult {
  std::string code;
  SourceMap source_map;
  std::vector<std::string> dependencies;
  CompilationReport report;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool success() const noexcept;
};

class Transpiler final {
 public:
  [[nodiscard]] TranspileResult transpile(std::string_view source,
                                          const TranspileOptions& options = {}) const;
};

[[nodiscard]] const char* to_string(SourceLanguage language) noexcept;
[[nodiscard]] SourceLanguage language_from_name(std::string_view name) noexcept;
[[nodiscard]] bool frontend_available(SourceLanguage language) noexcept;
[[nodiscard]] bool source_language_name_known(std::string_view name) noexcept;
[[nodiscard]] std::vector<SourceLanguage> registered_source_languages();
[[nodiscard]] const char* to_string(TargetLanguage language) noexcept;
[[nodiscard]] TargetLanguage target_from_name(std::string_view name) noexcept;
[[nodiscard]] bool target_language_name_known(std::string_view name) noexcept;
[[nodiscard]] std::vector<TargetLanguage> registered_target_languages();
[[nodiscard]] const char* to_string(FortranSourceForm form) noexcept;
[[nodiscard]] FortranSourceForm fortran_source_form_from_name(std::string_view name) noexcept;
[[nodiscard]] bool backend_available(TargetLanguage language) noexcept;

}  // namespace mpf
