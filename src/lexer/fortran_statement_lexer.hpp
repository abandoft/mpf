#pragma once

#include <vector>

#include "../source/source_text.hpp"
#include "statement_token.hpp"

namespace mpf::detail {

enum class FortranStatementTokenKind {
  end,
  identifier,
  number,
  string_literal,
  keyword_program,
  keyword_end,
  keyword_endif,
  keyword_enddo,
  keyword_implicit,
  keyword_none,
  keyword_contains,
  keyword_recursive,
  keyword_function,
  keyword_subroutine,
  keyword_result,
  keyword_return,
  keyword_if,
  keyword_then,
  keyword_else,
  keyword_elseif,
  keyword_select,
  keyword_case,
  keyword_default,
  keyword_endselect,
  keyword_do,
  keyword_while,
  keyword_exit,
  keyword_cycle,
  keyword_print,
  keyword_write,
  keyword_call,
  keyword_integer,
  keyword_real,
  keyword_double,
  keyword_precision,
  keyword_complex,
  keyword_logical,
  keyword_character,
  unsupported_keyword,
  left_parenthesis,
  right_parenthesis,
  left_bracket,
  right_bracket,
  comma,
  colon,
  double_colon,
  equal,
  star,
  other
};

using FortranStatementToken = BasicStatementToken<FortranStatementTokenKind>;

struct FortranStatementLine {
  SourceLine source;
  std::vector<FortranStatementToken> tokens;
};

struct FortranStatementLexResult {
  std::vector<FortranStatementLine> lines;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] FortranStatementLexResult lex_fortran_statements(std::vector<SourceLine> lines);
[[nodiscard]] const char* to_string(FortranStatementTokenKind kind) noexcept;

}  // namespace mpf::detail
