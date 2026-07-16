#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "logical_source.hpp"

namespace mpf::detail::logical_source {

struct Delimiter {
  char value{'\0'};
  std::size_t line{1};
  std::size_t column{1};
};

inline std::string trim(const std::string_view value) {
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

inline void add_error(std::vector<Diagnostic>& diagnostics, const std::size_t line,
                      const std::size_t column, std::string code, std::string message) {
  diagnostics.emplace_back(DiagnosticSeverity::error, std::move(code), std::move(message),
                           SourceLocation{line, column});
}

inline bool matching_delimiters(const char opening, const char closing) noexcept {
  return (opening == '(' && closing == ')') || (opening == '[' && closing == ']') ||
         (opening == '{' && closing == '}');
}

inline void append_joined(std::string& logical, const std::string_view fragment,
                          const bool delete_newline) {
  if (!logical.empty() && !fragment.empty() && !delete_newline &&
      std::isspace(static_cast<unsigned char>(logical.back())) == 0 &&
      std::isspace(static_cast<unsigned char>(fragment.front())) == 0) {
    logical.push_back(' ');
  }
  logical.append(fragment);
}

inline void push_delimiter(std::vector<Delimiter>& delimiters, const char value,
                           const std::size_t line, const std::size_t column) {
  delimiters.push_back({value, line, column});
}

inline void close_delimiter(std::vector<Delimiter>& delimiters,
                            std::vector<Diagnostic>& diagnostics, const char value,
                            const std::size_t line, const std::size_t column, const char* code,
                            const char* language) {
  if (delimiters.empty() || !matching_delimiters(delimiters.back().value, value)) {
    add_error(diagnostics, line, column, code,
              std::string("unmatched closing delimiter in ") + language + " source");
    return;
  }
  delimiters.pop_back();
}

}  // namespace mpf::detail::logical_source
