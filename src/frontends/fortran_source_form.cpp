#include "fortran_source_form.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace mpf::detail {
namespace {

struct Fragment {
  std::string code;
  bool continues{false};
};

std::string trim(std::string_view value) {
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }
  std::size_t last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
    --last;
  }
  return std::string(value.substr(first, last - first));
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::size_t first_non_space(const std::string_view value) noexcept {
  std::size_t index = 0;
  while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index])) != 0) {
    ++index;
  }
  return index;
}

void add_error(std::vector<Diagnostic>& diagnostics, const std::size_t line,
               const std::size_t column, std::string code, std::string message) {
  diagnostics.emplace_back(DiagnosticSeverity::error, std::move(code), std::move(message),
                           SourceLocation{line, column});
}

FortranSourceForm resolve_form(const SourceText& source, const FortranSourceForm requested) {
  if (requested != FortranSourceForm::automatic) return requested;
  const auto extension = lower(std::filesystem::path(source.filename()).extension().string());
  if (extension == ".f" || extension == ".for" || extension == ".ftn" || extension == ".f77") {
    return FortranSourceForm::fixed;
  }
  return FortranSourceForm::free;
}

Fragment scan_fragment(const std::string_view input, char& quote, const bool trailing_ampersand) {
  Fragment result;
  for (std::size_t index = 0; index < input.size(); ++index) {
    const auto character = input[index];
    if (quote != '\0') {
      result.code.push_back(character);
      if (character == quote) {
        if (index + 1 < input.size() && input[index + 1] == quote) {
          result.code.push_back(input[++index]);
        } else {
          quote = '\0';
        }
      }
      continue;
    }
    if (character == '\'' || character == '"') {
      quote = character;
      result.code.push_back(character);
    } else if (character == '!') {
      break;
    } else {
      result.code.push_back(character);
    }
  }

  while (!result.code.empty() &&
         std::isspace(static_cast<unsigned char>(result.code.back())) != 0) {
    result.code.pop_back();
  }
  if (trailing_ampersand && !result.code.empty() && result.code.back() == '&') {
    result.code.pop_back();
    while (!result.code.empty() &&
           std::isspace(static_cast<unsigned char>(result.code.back())) != 0) {
      result.code.pop_back();
    }
    result.continues = true;
  }
  return result;
}

void append_logical_statement(std::vector<SourceLine>& output, const std::string_view logical,
                              const std::size_t line, const std::size_t byte_offset) {
  std::size_t begin = 0;
  char quote = '\0';
  for (std::size_t index = 0; index <= logical.size(); ++index) {
    if (index < logical.size()) {
      const auto character = logical[index];
      if (quote != '\0') {
        if (character == quote) {
          if (index + 1 < logical.size() && logical[index + 1] == quote) {
            ++index;
          } else {
            quote = '\0';
          }
        }
      } else if (character == '\'' || character == '"') {
        quote = character;
      }
    }
    if (index == logical.size() || (quote == '\0' && logical[index] == ';')) {
      auto statement = trim(logical.substr(begin, index - begin));
      if (!statement.empty()) output.push_back({line, byte_offset, 0, std::move(statement)});
      begin = index + 1;
    }
  }
}

FortranLogicalLinesResult normalize_free(const SourceText& source) {
  FortranLogicalLinesResult result;
  result.source_form = FortranSourceForm::free;
  const auto physical_lines = source.lines(false);
  std::string logical;
  std::size_t logical_line = 1;
  std::size_t logical_offset = 0;
  bool continuing = false;
  char quote = '\0';

  for (const auto& physical : physical_lines) {
    auto raw = source.line_text(physical.number);
    const auto first = first_non_space(raw);
    if (first == raw.size() || raw[first] == '!') continue;
    if (raw[first] == '#') {
      add_error(result.diagnostics, physical.number, first + 1, "MPF1303",
                "Fortran preprocessing directives are not supported in the current subset");
      continue;
    }

    bool leading_ampersand = false;
    if (raw[first] == '&') {
      leading_ampersand = true;
      raw.remove_prefix(first + 1);
    } else {
      raw.remove_prefix(first);
    }
    if (!continuing && leading_ampersand) {
      add_error(result.diagnostics, physical.number, first + 1, "MPF1301",
                "Fortran continuation marker has no preceding continued statement");
    }
    if (continuing && quote != '\0' && !leading_ampersand) {
      add_error(result.diagnostics, physical.number, first + 1, "MPF1304",
                "continued Fortran character literal requires a leading '&' marker");
    }
    if (!continuing) {
      logical.clear();
      logical_line = physical.number;
      logical_offset = physical.byte_offset;
      quote = '\0';
    }

    auto fragment = scan_fragment(raw, quote, true);
    logical += fragment.code;
    continuing = fragment.continues;
    if (!continuing) {
      if (quote != '\0') {
        add_error(result.diagnostics, physical.number, 1, "MPF1305",
                  "unterminated Fortran character literal");
        quote = '\0';
      }
      append_logical_statement(result.lines, logical, logical_line, logical_offset);
      logical.clear();
    }
  }

  if (continuing) {
    add_error(result.diagnostics, logical_line, 1, "MPF1302",
              "continued Fortran statement reaches end of file");
  }
  return result;
}

bool valid_label_field(const std::string_view field) noexcept {
  return std::all_of(field.begin(), field.end(), [](const char character) {
    return character == ' ' || std::isdigit(static_cast<unsigned char>(character)) != 0;
  });
}

FortranLogicalLinesResult normalize_fixed(const SourceText& source) {
  FortranLogicalLinesResult result;
  result.source_form = FortranSourceForm::fixed;
  const auto physical_lines = source.lines(false);
  std::string logical;
  std::size_t logical_line = 1;
  std::size_t logical_offset = 0;
  bool active = false;
  char quote = '\0';

  const auto flush = [&] {
    if (!active) return;
    if (quote != '\0') {
      add_error(result.diagnostics, logical_line, 7, "MPF1305",
                "unterminated fixed-form Fortran character literal");
      quote = '\0';
    }
    append_logical_statement(result.lines, logical, logical_line, logical_offset);
    logical.clear();
    active = false;
  };

  for (const auto& physical : physical_lines) {
    const auto raw = source.line_text(physical.number);
    if (raw.empty()) continue;
    const auto first = first_non_space(raw);
    if (first == raw.size()) continue;
    const auto first_character = raw.front();
    if (first_character == 'c' || first_character == 'C' || first_character == '*' ||
        first_character == '!') {
      continue;
    }
    if (raw[first] == '#') {
      add_error(result.diagnostics, physical.number, first + 1, "MPF1303",
                "Fortran preprocessing directives are not supported in the current subset");
      continue;
    }
    const auto layout_width = std::min<std::size_t>(6, raw.size());
    if (raw.substr(0, layout_width).find('\t') != std::string_view::npos) {
      add_error(result.diagnostics, physical.number, 1, "MPF1306",
                "tab-form fixed Fortran source is not supported; use standard columns 1-6");
      continue;
    }
    const auto label_width = std::min<std::size_t>(5, raw.size());
    if (!valid_label_field(raw.substr(0, label_width))) {
      add_error(result.diagnostics, physical.number, 1, "MPF1307",
                "fixed-form Fortran label field must contain only spaces or digits");
      continue;
    }

    const bool continuation = raw.size() > 5 && raw[5] != ' ' && raw[5] != '0';
    const auto field_begin = std::min<std::size_t>(6, raw.size());
    const auto field_end = std::min<std::size_t>(72, raw.size());
    auto statement_field = raw.substr(field_begin, field_end - field_begin);
    if (continuation) {
      if (!active) {
        add_error(result.diagnostics, physical.number, 6, "MPF1301",
                  "fixed-form continuation line has no preceding statement");
        logical_line = physical.number;
        logical_offset = physical.byte_offset;
        quote = '\0';
        active = true;
      }
    } else {
      flush();
      logical_line = physical.number;
      logical_offset = physical.byte_offset;
      quote = '\0';
      active = true;
    }
    auto fragment = scan_fragment(statement_field, quote, false);
    logical += fragment.code;
  }
  flush();
  return result;
}

}  // namespace

FortranLogicalLinesResult normalize_fortran_source(const SourceText& source,
                                                   const FortranSourceForm requested_form) {
  const auto form = resolve_form(source, requested_form);
  return form == FortranSourceForm::fixed ? normalize_fixed(source) : normalize_free(source);
}

}  // namespace mpf::detail
