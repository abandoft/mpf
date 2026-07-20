#pragma once

#include <vector>

#include "lexer/statement_token.hpp"
#include "source/source_text.hpp"

namespace mpf::detail {

enum class MatlabStatementTokenKind {
  end,
  identifier,
  number,
  string_literal,
  keyword_function,
  keyword_if,
  keyword_elseif,
  keyword_else,
  keyword_end,
  keyword_while,
  keyword_for,
  keyword_switch,
  keyword_case,
  keyword_otherwise,
  keyword_break,
  keyword_continue,
  keyword_return,
  keyword_try,
  keyword_catch,
  keyword_arguments,
  unsupported_keyword,
  left_parenthesis,
  right_parenthesis,
  left_bracket,
  right_bracket,
  left_brace,
  right_brace,
  comma,
  colon,
  semicolon,
  equal,
  transpose,
  other
};

using MatlabStatementToken = BasicStatementToken<MatlabStatementTokenKind>;

struct MatlabStatementLine {
  SourceLine source;
  std::vector<MatlabStatementToken> tokens;
};

struct MatlabStatementLexResult {
  std::vector<MatlabStatementLine> lines;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] MatlabStatementLexResult lex_matlab_statements(std::vector<SourceLine> lines);
[[nodiscard]] const char* to_string(MatlabStatementTokenKind kind) noexcept;

}  // namespace mpf::detail
