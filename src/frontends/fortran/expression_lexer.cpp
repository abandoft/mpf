#include "frontends/fortran/expression_lexer.hpp"

#include <iterator>

#include "lexer/scanner.hpp"

namespace mpf::detail {
namespace {

constexpr ExpressionSymbol symbols[]{
    {"(/", TokenKind::left_bracket},
    {"/)", TokenKind::right_bracket},
    {"**", TokenKind::power},
    {"==", TokenKind::equal_equal},
    {"/=", TokenKind::not_equal},
    {"<=", TokenKind::less_equal},
    {">=", TokenKind::greater_equal},
    {"+", TokenKind::plus},
    {"-", TokenKind::minus},
    {"*", TokenKind::star},
    {"/", TokenKind::slash},
    {"%", TokenKind::percent},
    {"<", TokenKind::less},
    {">", TokenKind::greater},
    {"=", TokenKind::equal},
    {"(", TokenKind::left_parenthesis},
    {")", TokenKind::right_parenthesis},
    {"[", TokenKind::left_bracket},
    {"]", TokenKind::right_bracket},
    {",", TokenKind::comma},
    {":", TokenKind::colon},
    {".", TokenKind::dot},
};

constexpr ExpressionScannerProfile scanner_profile{
    nullptr, 0,    symbols, std::size(symbols), StringEscapeMode::doubled_quote, false, true, false,
    true,    true, false,
};

}  // namespace

LexerResult lex_fortran_expression(const std::string_view input, const std::size_t line,
                                   const std::size_t column) {
  return scan_expression(input, scanner_profile, line, column);
}

}  // namespace mpf::detail
