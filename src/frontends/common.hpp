#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../compiler/expression.hpp"
#include "../compiler/ir.hpp"

namespace mpf::detail::frontend {

inline std::string trim(std::string_view value) {
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

inline std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

inline bool starts_with_ci(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(value[index])) !=
        std::tolower(static_cast<unsigned char>(prefix[index]))) {
      return false;
    }
  }
  return true;
}

inline bool contains_ci(const std::string_view value, const std::string_view needle) {
  if (needle.empty()) return true;
  if (needle.size() > value.size()) return false;
  for (std::size_t offset = 0; offset + needle.size() <= value.size(); ++offset) {
    if (starts_with_ci(value.substr(offset), needle)) return true;
  }
  return false;
}

inline std::vector<std::string> comma_list(std::string_view value) {
  std::vector<std::string> result;
  std::size_t begin = 0;
  int depth = 0;
  char quote = '\0';
  for (std::size_t index = 0; index <= value.size(); ++index) {
    if (index < value.size()) {
      const auto character = value[index];
      if (quote != '\0') {
        if (character == quote) {
          if (index + 1 < value.size() && value[index + 1] == quote) {
            ++index;
          } else {
            quote = '\0';
          }
        } else if (character == '\\' && index + 1 < value.size()) {
          ++index;
        }
      } else if (character == '\'' || character == '"') {
        quote = character;
      } else if (character == '(' || character == '[' || character == '{') {
        ++depth;
      } else if (character == ')' || character == ']' || character == '}') {
        --depth;
      }
    }
    if (index == value.size() || (quote == '\0' && depth == 0 && value[index] == ',')) {
      auto item = trim(value.substr(begin, index - begin));
      if (!item.empty()) result.push_back(std::move(item));
      begin = index + 1;
    }
  }
  return result;
}

inline void append_expression(Statement& statement, std::string_view expression,
                              const SourceLanguage language, const std::size_t line,
                              std::vector<Diagnostic>& diagnostics) {
  if (trim(expression).empty()) {
    statement.has_expression = false;
    return;
  }
  auto translated = parse_expression(expression, language, line);
  statement.expression = std::move(translated.expression);
  statement.has_expression = statement.expression.valid();
  diagnostics.insert(diagnostics.end(), std::make_move_iterator(translated.diagnostics.begin()),
                     std::make_move_iterator(translated.diagnostics.end()));
}

inline void append_expression(Expression& destination, bool& present, std::string_view expression,
                              const SourceLanguage language, const std::size_t line,
                              std::vector<Diagnostic>& diagnostics) {
  if (trim(expression).empty()) {
    present = false;
    return;
  }
  auto parsed = parse_expression(expression, language, line);
  destination = std::move(parsed.expression);
  present = destination.valid();
  diagnostics.insert(diagnostics.end(), std::make_move_iterator(parsed.diagnostics.begin()),
                     std::make_move_iterator(parsed.diagnostics.end()));
}

inline void unsupported(std::vector<Diagnostic>& diagnostics, const std::size_t line,
                        std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF1200", std::move(message), {line, 1}});
}

inline void version_unsupported(std::vector<Diagnostic>& diagnostics, const std::size_t line,
                                std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF1201", std::move(message), {line, 1}});
}

inline bool valid_identifier(std::string_view value) {
  if (value.empty()) return false;
  const auto first = static_cast<unsigned char>(value.front());
  if (std::isalpha(first) == 0 && value.front() != '_') return false;
  return std::all_of(value.begin() + 1, value.end(), [](const char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '_';
  });
}

}  // namespace mpf::detail::frontend
