#include "frontends/matlab/logical_source.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "frontends/common/logical_source_support.hpp"

namespace mpf::detail {
namespace {

using logical_source::Delimiter;

struct MatlabFragment {
  std::string code;
  bool explicit_join{false};
};

bool matlab_string_starts(const std::string& code) noexcept {
  std::size_t index = code.size();
  while (index > 0 && std::isspace(static_cast<unsigned char>(code[index - 1])) != 0) {
    --index;
  }
  if (index == 0) return true;
  const auto previous = code[index - 1];
  const std::string_view prefix_characters{"=([{,:;+-*/\\^~<>&|"};
  return prefix_characters.find(previous) != std::string_view::npos || previous == '\'' ||
         previous == '"';
}

MatlabFragment scan_matlab_fragment(const std::string_view input, const std::size_t line,
                                    std::vector<Delimiter>& delimiters,
                                    std::vector<Diagnostic>& diagnostics) {
  MatlabFragment result;
  char quote = '\0';
  std::size_t quote_column = 1;
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

    if (character == '%') break;
    if (character == '.' && index + 2 < input.size() && input[index + 1] == '.' &&
        input[index + 2] == '.') {
      result.explicit_join = true;
      break;
    }
    if (character == '"' || (character == '\'' && matlab_string_starts(result.code))) {
      quote = character;
      quote_column = index + 1;
      result.code.push_back(character);
      continue;
    }
    if (character == '(' || character == '[' || character == '{') {
      logical_source::push_delimiter(delimiters, character, line, index + 1);
    } else if (character == ')' || character == ']' || character == '}') {
      logical_source::close_delimiter(delimiters, diagnostics, character, line, index + 1,
                                      "MPF1501", "Matlab");
    }
    result.code.push_back(character);
  }
  if (quote != '\0') {
    logical_source::add_error(diagnostics, line, quote_column, "MPF1503",
                              "unterminated Matlab string literal");
  }
  while (!result.code.empty() &&
         std::isspace(static_cast<unsigned char>(result.code.back())) != 0) {
    result.code.pop_back();
  }
  return result;
}

void append_matlab_statements(std::vector<SourceLine>& output, const std::string_view logical,
                              const std::size_t line, const std::size_t byte_offset) {
  std::size_t begin = 0;
  int depth = 0;
  char quote = '\0';
  std::string scanned;
  for (std::size_t index = 0; index <= logical.size(); ++index) {
    if (index < logical.size()) {
      const auto character = logical[index];
      if (quote != '\0') {
        if (character == quote) {
          if (index + 1 < logical.size() && logical[index + 1] == quote) {
            scanned.push_back(character);
            scanned.push_back(logical[++index]);
            continue;
          }
          quote = '\0';
        }
      } else if (character == '"' || (character == '\'' && matlab_string_starts(scanned))) {
        quote = character;
      } else if (character == '(' || character == '[' || character == '{') {
        ++depth;
      } else if (character == ')' || character == ']' || character == '}') {
        --depth;
      }
      scanned.push_back(character);
    }
    if (index == logical.size() ||
        (quote == '\0' && depth == 0 && (logical[index] == ';' || logical[index] == ','))) {
      auto statement = logical_source::trim(logical.substr(begin, index - begin));
      if (!statement.empty()) output.push_back({line, byte_offset, 0, std::move(statement)});
      begin = index + 1;
      scanned.clear();
    }
  }
}

}  // namespace

LogicalSourceResult normalize_matlab_source(const SourceText& source) {
  LogicalSourceResult result;
  const auto physical_lines = source.lines(false);
  std::string logical;
  std::vector<Delimiter> delimiters;
  std::size_t logical_line = 1;
  std::size_t logical_offset = 0;
  bool active = false;
  bool block_comment = false;
  bool explicit_join = false;
  bool pending_matrix_row = false;

  for (const auto& physical : physical_lines) {
    const auto raw = source.line_text(physical.number);
    const auto stripped = logical_source::trim(raw);
    if (block_comment) {
      if (stripped == "%}") block_comment = false;
      continue;
    }
    if (!active && stripped == "%{") {
      block_comment = true;
      continue;
    }
    if (!active && (stripped.empty() || stripped.front() == '%')) continue;
    if (!active) {
      logical.clear();
      delimiters.clear();
      logical_line = physical.number;
      logical_offset = physical.byte_offset;
      pending_matrix_row = false;
      active = true;
    }

    auto fragment = scan_matlab_fragment(raw, physical.number, delimiters, result.diagnostics);
    const auto fragment_text = logical_source::trim(fragment.code);
    if (pending_matrix_row && !fragment_text.empty()) {
      if (fragment_text.front() != ']') logical.push_back(';');
      pending_matrix_row = false;
    }
    logical_source::append_joined(logical, fragment.code, false);
    explicit_join = fragment.explicit_join;

    const bool matrix_continuation =
        !fragment.explicit_join && !delimiters.empty() && delimiters.back().value == '[';
    bool matrix_row = matrix_continuation;
    if (matrix_row && delimiters.back().line == physical.number) {
      const auto opening = delimiters.back().column - 1;
      matrix_row =
          opening + 1 < fragment.code.size() &&
          !logical_source::trim(std::string_view(fragment.code).substr(opening + 1)).empty();
    } else if (matrix_row) {
      matrix_row = !logical_source::trim(fragment.code).empty();
    }
    if (matrix_row) pending_matrix_row = true;
    if (!fragment.explicit_join && !matrix_continuation && !delimiters.empty()) {
      const auto opening = delimiters.front();
      logical_source::add_error(
          result.diagnostics, opening.line, opening.column, "MPF1502",
          "Matlab delimiter crosses a physical line without an ellipsis continuation");
      delimiters.clear();
    }

    if (!fragment.explicit_join && !matrix_continuation) {
      append_matlab_statements(result.lines, logical, logical_line, logical_offset);
      logical.clear();
      active = false;
    }
  }

  if (block_comment) {
    logical_source::add_error(result.diagnostics, source.line_count(), 1, "MPF1504",
                              "Matlab block comment reaches end of file without '%}'");
  }
  if (active) {
    if (!delimiters.empty()) {
      const auto opening = delimiters.front();
      logical_source::add_error(result.diagnostics, opening.line, opening.column, "MPF1502",
                                "unclosed delimiter in Matlab logical line");
    } else if (explicit_join) {
      logical_source::add_error(result.diagnostics, logical_line, 1, "MPF1505",
                                "Matlab ellipsis continuation reaches end of file");
    }
  }
  return result;
}

}  // namespace mpf::detail
