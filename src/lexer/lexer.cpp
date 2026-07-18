#include "lexer.hpp"

namespace mpf::detail {

const char* to_string(const TokenKind kind) noexcept {
  switch (kind) {
    case TokenKind::end: return "end of expression";
    case TokenKind::identifier: return "identifier";
    case TokenKind::number: return "number";
    case TokenKind::string_literal: return "string";
    case TokenKind::true_keyword: return "true";
    case TokenKind::false_keyword: return "false";
    case TokenKind::null_keyword: return "null";
    case TokenKind::plus: return "+";
    case TokenKind::minus: return "-";
    case TokenKind::star: return "*";
    case TokenKind::slash: return "/";
    case TokenKind::backslash: return "\\";
    case TokenKind::dot_star: return ".*";
    case TokenKind::dot_slash: return "./";
    case TokenKind::dot_backslash: return ".\\";
    case TokenKind::dot_power: return ".^";
    case TokenKind::floor_slash: return "//";
    case TokenKind::percent: return "%";
    case TokenKind::power: return "**";
    case TokenKind::equal_equal: return "==";
    case TokenKind::not_equal: return "!=";
    case TokenKind::less: return "<";
    case TokenKind::less_equal: return "<=";
    case TokenKind::greater: return ">";
    case TokenKind::greater_equal: return ">=";
    case TokenKind::logical_and: return "and";
    case TokenKind::logical_or: return "or";
    case TokenKind::elementwise_logical_and: return "&";
    case TokenKind::elementwise_logical_or: return "|";
    case TokenKind::logical_not: return "not";
    case TokenKind::identity_is: return "is";
    case TokenKind::membership_in: return "in";
    case TokenKind::conditional_if: return "if";
    case TokenKind::conditional_else: return "else";
    case TokenKind::equal: return "=";
    case TokenKind::left_parenthesis: return "(";
    case TokenKind::right_parenthesis: return ")";
    case TokenKind::left_bracket: return "[";
    case TokenKind::right_bracket: return "]";
    case TokenKind::comma: return ",";
    case TokenKind::colon: return ":";
    case TokenKind::semicolon: return ";";
    case TokenKind::dot: return ".";
    case TokenKind::transpose: return ".'";
    case TokenKind::conjugate_transpose: return "'";
  }
  return "token";
}

}  // namespace mpf::detail
