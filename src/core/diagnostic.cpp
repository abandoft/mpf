#include "mpf/diagnostic.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#include "../source/source_text.hpp"

namespace mpf {
namespace {

std::string json_escape(const std::string_view value) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
      case '"': output << "\\\""; break;
      case '\\': output << "\\\\"; break;
      case '\b': output << "\\b"; break;
      case '\f': output << "\\f"; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default:
        if (character < 0x20U) {
          output << "\\u" << std::setw(4) << static_cast<unsigned int>(character);
        } else {
          output << static_cast<char>(character);
        }
        break;
    }
  }
  return output.str();
}

std::string caret_padding(const std::string_view line, const std::size_t one_based_column) {
  std::string padding;
  std::size_t column = 1;
  for (std::size_t index = 0; index < line.size() && column < one_based_column; ++index) {
    const auto byte = static_cast<unsigned char>(line[index]);
    if ((byte & 0xC0U) == 0x80U) continue;
    padding.push_back(line[index] == '\t' ? '\t' : ' ');
    ++column;
  }
  while (column < one_based_column) {
    padding.push_back(' ');
    ++column;
  }
  return padding;
}

const char* severity_color(const DiagnosticSeverity severity) noexcept {
  switch (severity) {
    case DiagnosticSeverity::note: return "\033[36m";
    case DiagnosticSeverity::warning: return "\033[33m";
    case DiagnosticSeverity::error: return "\033[31m";
  }
  return "";
}

}  // namespace

const char* to_string(const DiagnosticSeverity severity) noexcept {
  switch (severity) {
    case DiagnosticSeverity::note: return "note";
    case DiagnosticSeverity::warning: return "warning";
    case DiagnosticSeverity::error: return "error";
  }
  return "error";
}

std::string render_diagnostic_text(const Diagnostic& diagnostic, const std::string_view source,
                                   const DiagnosticRenderOptions& options) {
  std::ostringstream output;
  const auto source_name = diagnostic.source_name.empty()
                               ? std::string_view("<unknown>")
                               : std::string_view(diagnostic.source_name);
  output << source_name << ':' << diagnostic.location.line << ':' << diagnostic.location.column
         << ": ";
  if (options.use_color) output << severity_color(diagnostic.severity);
  output << to_string(diagnostic.severity) << ' ' << diagnostic.code;
  if (options.use_color) output << "\033[0m";
  output << ": " << diagnostic.message << '\n';

  if (!options.show_source || source.empty() || diagnostic.location.line == 0) {
    return output.str();
  }
  const detail::SourceText source_text(source);
  const auto line = source_text.line_text(diagnostic.location.line);
  if (line.empty() && diagnostic.location.line > source_text.line_count()) return output.str();

  const auto line_number = std::to_string(diagnostic.location.line);
  output << ' ' << line_number << " | " << line << '\n';
  output << std::string(line_number.size() + 2, ' ') << "| "
         << caret_padding(line, std::max<std::size_t>(1, diagnostic.location.column));
  if (options.use_color) output << severity_color(diagnostic.severity);
  output << '^';
  const auto end = diagnostic.end_location;
  if (end.line == diagnostic.location.line && end.column > diagnostic.location.column + 1) {
    output << std::string(end.column - diagnostic.location.column - 1, '~');
  }
  if (options.use_color) output << "\033[0m";
  output << '\n';
  return output.str();
}

std::string render_diagnostics_json(const std::vector<Diagnostic>& diagnostics) {
  std::ostringstream output;
  output << "{\"schemaVersion\":1,\"diagnostics\":[";
  for (std::size_t index = 0; index < diagnostics.size(); ++index) {
    if (index != 0) output << ',';
    const auto& diagnostic = diagnostics[index];
    const auto end = diagnostic.end_location;
    output << "{\"severity\":\"" << to_string(diagnostic.severity) << "\",\"code\":\""
           << json_escape(diagnostic.code) << "\",\"message\":\"" << json_escape(diagnostic.message)
           << "\",\"source\":\"" << json_escape(diagnostic.source_name)
           << "\",\"sourceId\":" << diagnostic.source_id
           << ",\"range\":{\"start\":{\"line\":" << diagnostic.location.line
           << ",\"column\":" << diagnostic.location.column << "},\"end\":{\"line\":" << end.line
           << ",\"column\":" << end.column << "}}}";
  }
  output << "]}";
  return output.str();
}

}  // namespace mpf
