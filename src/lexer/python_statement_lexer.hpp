#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../source/source_text.hpp"
#include "statement_token.hpp"

namespace mpf::detail {

enum class PythonStatementTokenKind {
  end,
  identifier,
  number,
  string_literal,
  keyword_def,
  keyword_if,
  keyword_elif,
  keyword_else,
  keyword_while,
  keyword_for,
  keyword_in,
  keyword_return,
  keyword_break,
  keyword_continue,
  unsupported_keyword,
  left_parenthesis,
  right_parenthesis,
  left_bracket,
  right_bracket,
  left_brace,
  right_brace,
  comma,
  colon,
  equal,
  slash,
  star,
  arrow,
  other
};

using PythonStatementToken = BasicStatementToken<PythonStatementTokenKind>;

struct PythonStatementLine {
  SourceLine source;
  std::vector<PythonStatementToken> tokens;
};

struct PythonStatementLexResult {
  std::vector<PythonStatementLine> lines;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] PythonStatementLexResult lex_python_statements(std::vector<SourceLine> lines);
[[nodiscard]] const char* to_string(PythonStatementTokenKind kind) noexcept;

}  // namespace mpf::detail
