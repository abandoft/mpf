#include "frontends/python/logical_source.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "frontends/common/logical_source_support.hpp"

namespace mpf::detail {
namespace {

using logical_source::Delimiter;

struct PythonFragment {
  std::string code;
  bool explicit_join{false};
  bool comment{false};
};

std::pair<std::size_t, std::size_t> python_indentation(const std::string_view value) noexcept {
  std::size_t index = 0;
  std::size_t column = 0;
  while (index < value.size()) {
    if (value[index] == ' ') {
      ++column;
    } else if (value[index] == '\t') {
      column = ((column / 8) + 1) * 8;
    } else if (value[index] == '\f') {
      column = 0;
    } else {
      break;
    }
    ++index;
  }
  return {index, column};
}

PythonFragment scan_python_fragment(const std::string_view input, const std::size_t line,
                                    char& quote, std::size_t& quote_line, std::size_t& quote_column,
                                    std::vector<Delimiter>& delimiters,
                                    std::vector<Diagnostic>& diagnostics) {
  PythonFragment result;
  for (std::size_t index = 0; index < input.size(); ++index) {
    const auto character = input[index];
    if (quote != '\0') {
      if (character == '\\') {
        result.code.push_back(character);
        if (index + 1 < input.size()) result.code.push_back(input[++index]);
        continue;
      }
      result.code.push_back(character);
      if (character == quote) quote = '\0';
      continue;
    }

    if (character == '#') {
      result.comment = true;
      break;
    }
    if (character == '\'' || character == '"') {
      if (index + 2 < input.size() && input[index + 1] == character &&
          input[index + 2] == character) {
        logical_source::add_error(
            diagnostics, line, index + 1, "MPF1405",
            "triple-quoted Python strings are not supported in the current subset");
      }
      quote = character;
      quote_line = line;
      quote_column = index + 1;
      result.code.push_back(character);
      continue;
    }
    if (character == '(' || character == '[' || character == '{') {
      logical_source::push_delimiter(delimiters, character, line, index + 1);
    } else if (character == ')' || character == ']' || character == '}') {
      logical_source::close_delimiter(delimiters, diagnostics, character, line, index + 1,
                                      "MPF1401", "Python");
    }
    result.code.push_back(character);
  }

  auto last = result.code.size();
  while (last > 0 && std::isspace(static_cast<unsigned char>(result.code[last - 1])) != 0) {
    --last;
  }
  const bool trailing_space = last < result.code.size();
  result.code.erase(last);
  if (!result.code.empty() && result.code.back() == '\\') {
    if (result.comment || trailing_space) {
      logical_source::add_error(diagnostics, line, result.code.size(), "MPF1403",
                                "Python explicit line continuation must end the physical line");
      result.code.pop_back();
    } else {
      result.code.pop_back();
      result.explicit_join = true;
    }
  }
  if (quote != '\0' && !result.explicit_join) {
    logical_source::add_error(diagnostics, quote_line, quote_column, "MPF1404",
                              "unterminated Python string literal");
    quote = '\0';
  }
  return result;
}

void append_python_statements(std::vector<SourceLine>& output, const std::string_view logical,
                              const std::size_t line, const std::size_t byte_offset,
                              const std::size_t indent) {
  std::size_t begin = 0;
  int depth = 0;
  char quote = '\0';
  bool escaped = false;
  for (std::size_t index = 0; index <= logical.size(); ++index) {
    if (index < logical.size()) {
      const auto character = logical[index];
      if (quote != '\0') {
        if (escaped) {
          escaped = false;
        } else if (character == '\\') {
          escaped = true;
        } else if (character == quote) {
          quote = '\0';
        }
      } else if (character == '\'' || character == '"') {
        quote = character;
      } else if (character == '(' || character == '[' || character == '{') {
        ++depth;
      } else if (character == ')' || character == ']' || character == '}') {
        --depth;
      }
    }
    if (index == logical.size() || (quote == '\0' && depth == 0 && logical[index] == ';')) {
      auto statement = logical_source::trim(logical.substr(begin, index - begin));
      if (!statement.empty()) {
        output.push_back({line, byte_offset, indent, std::move(statement)});
      }
      begin = index + 1;
    }
  }
}

}  // namespace

LogicalSourceResult normalize_python_source(const SourceText& source) {
  LogicalSourceResult result;
  const auto physical_lines = source.lines(false);
  std::string logical;
  std::vector<Delimiter> delimiters;
  char quote = '\0';
  std::size_t quote_line = 1;
  std::size_t quote_column = 1;
  std::size_t logical_line = 1;
  std::size_t logical_offset = 0;
  std::size_t logical_indent = 0;
  bool active = false;
  bool delete_newline = false;

  for (const auto& physical : physical_lines) {
    const auto raw = source.line_text(physical.number);
    const auto [content_begin, indent] = python_indentation(raw);
    if (!active && (content_begin == raw.size() ||
                    (content_begin < raw.size() && raw[content_begin] == '#'))) {
      continue;
    }
    if (!active) {
      logical.clear();
      delimiters.clear();
      quote = '\0';
      logical_line = physical.number;
      logical_offset = physical.byte_offset;
      logical_indent = indent;
      active = true;
      delete_newline = false;
    }

    const auto fragment_input =
        active && physical.number != logical_line ? raw : raw.substr(content_begin);
    auto fragment = scan_python_fragment(fragment_input, physical.number, quote, quote_line,
                                         quote_column, delimiters, result.diagnostics);
    logical_source::append_joined(logical, fragment.code, delete_newline);
    const bool continues = fragment.explicit_join || !delimiters.empty() || quote != '\0';
    delete_newline = fragment.explicit_join;
    if (!continues) {
      append_python_statements(result.lines, logical, logical_line, logical_offset, logical_indent);
      logical.clear();
      active = false;
    }
  }

  if (active) {
    if (quote != '\0') {
      logical_source::add_error(result.diagnostics, quote_line, quote_column, "MPF1404",
                                "continued Python string literal reaches end of file");
    }
    if (!delimiters.empty()) {
      const auto opening = delimiters.front();
      logical_source::add_error(result.diagnostics, opening.line, opening.column, "MPF1402",
                                "unclosed delimiter in Python logical line");
    } else if (delete_newline) {
      logical_source::add_error(result.diagnostics, logical_line, 1, "MPF1403",
                                "Python explicit line continuation reaches end of file");
    }
  }
  return result;
}

}  // namespace mpf::detail
