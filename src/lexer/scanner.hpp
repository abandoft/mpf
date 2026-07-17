#pragma once

#include <cstddef>

#include "lexer.hpp"

namespace mpf::detail {

struct ExpressionKeyword {
  std::string_view spelling;
  TokenKind kind{TokenKind::identifier};
};

struct ExpressionSymbol {
  std::string_view spelling;
  TokenKind kind{TokenKind::end};
};

enum class StringEscapeMode { backslash, doubled_quote };

struct ExpressionScannerProfile {
  const ExpressionKeyword* keywords{nullptr};
  std::size_t keyword_count{0};
  const ExpressionSymbol* symbols{nullptr};
  std::size_t symbol_count{0};
  StringEscapeMode string_escape_mode{StringEscapeMode::backslash};
  bool case_sensitive_keywords{true};
  bool normalize_identifiers_to_lowercase{false};
  bool matrix_whitespace_separates_elements{false};
  bool dotted_operators{false};
  bool fortran_numeric_literals{false};
  bool transpose_operators{false};
};

[[nodiscard]] LexerResult scan_expression(std::string_view input,
                                          const ExpressionScannerProfile& profile, std::size_t line,
                                          std::size_t column);

}  // namespace mpf::detail
