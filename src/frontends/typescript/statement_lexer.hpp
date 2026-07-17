#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "lexer/statement_token.hpp"
#include "source/source_text.hpp"

namespace mpf::detail {

enum class TypeScriptStatementTokenKind {
  end,
  newline,
  identifier,
  number,
  string_literal,
  keyword_function,
  keyword_let,
  keyword_const,
  keyword_export,
  keyword_if,
  keyword_else,
  keyword_while,
  keyword_for,
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
  semicolon,
  equal,
  question,
  dot,
  strict_equal,
  strict_not_equal,
  arrow,
  other
};

using TypeScriptStatementToken = BasicStatementToken<TypeScriptStatementTokenKind>;

struct TypeScriptStatementLexResult {
  std::vector<TypeScriptStatementToken> tokens;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] TypeScriptStatementLexResult lex_typescript_statements(const SourceText& source);
[[nodiscard]] const char* to_string(TypeScriptStatementTokenKind kind) noexcept;

}  // namespace mpf::detail
