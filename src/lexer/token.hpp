#pragma once

#include <string>

#include "mpf/diagnostic.hpp"

namespace mpf::detail {

enum class TokenKind {
  end,
  identifier,
  number,
  string_literal,
  true_keyword,
  false_keyword,
  null_keyword,
  plus,
  minus,
  star,
  slash,
  backslash,
  dot_star,
  dot_slash,
  dot_backslash,
  dot_power,
  floor_slash,
  percent,
  power,
  equal_equal,
  not_equal,
  less,
  less_equal,
  greater,
  greater_equal,
  logical_and,
  logical_or,
  logical_not,
  identity_is,
  membership_in,
  conditional_if,
  conditional_else,
  equal,
  left_parenthesis,
  right_parenthesis,
  left_bracket,
  right_bracket,
  comma,
  colon,
  semicolon,
  dot,
  transpose,
  conjugate_transpose
};

struct Token {
  TokenKind kind{TokenKind::end};
  std::string text;
  SourceLocation location{};
};

[[nodiscard]] const char* to_string(TokenKind kind) noexcept;

}  // namespace mpf::detail
