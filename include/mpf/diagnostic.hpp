#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mpf {

enum class DiagnosticSeverity { note, warning, error };

struct SourceLocation {
  std::size_t line{1};
  std::size_t column{1};
};

struct Diagnostic {
  Diagnostic(DiagnosticSeverity diagnostic_severity, std::string diagnostic_code,
             std::string diagnostic_message, SourceLocation diagnostic_location,
             std::string diagnostic_source_name = {}, std::size_t diagnostic_source_id = 0)
      : Diagnostic(diagnostic_severity, std::move(diagnostic_code), std::move(diagnostic_message),
                   diagnostic_location, {diagnostic_location.line, diagnostic_location.column + 1},
                   std::move(diagnostic_source_name), diagnostic_source_id) {}
  Diagnostic(DiagnosticSeverity diagnostic_severity, std::string diagnostic_code,
             std::string diagnostic_message, SourceLocation diagnostic_location,
             SourceLocation diagnostic_end_location, std::string diagnostic_source_name = {},
             std::size_t diagnostic_source_id = 0)
      : severity(diagnostic_severity),
        code(std::move(diagnostic_code)),
        message(std::move(diagnostic_message)),
        location(diagnostic_location),
        end_location(diagnostic_end_location),
        source_name(std::move(diagnostic_source_name)),
        source_id(diagnostic_source_id) {}

  DiagnosticSeverity severity{DiagnosticSeverity::error};
  std::string code;
  std::string message;
  SourceLocation location{};
  SourceLocation end_location{1, 2};
  std::string source_name;
  std::size_t source_id{0};
};

struct DiagnosticRenderOptions {
  bool show_source{true};
  bool use_color{false};
};

[[nodiscard]] const char* to_string(DiagnosticSeverity severity) noexcept;
[[nodiscard]] std::string render_diagnostic_text(const Diagnostic& diagnostic,
                                                 std::string_view source = {},
                                                 const DiagnosticRenderOptions& options = {});
[[nodiscard]] std::string render_diagnostics_json(const std::vector<Diagnostic>& diagnostics);

}  // namespace mpf
