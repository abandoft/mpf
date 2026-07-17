#include "frontends/python/expression_lexer.hpp"

#include <iterator>

#include "lexer/scanner.hpp"

namespace mpf::detail {
namespace {

constexpr ExpressionKeyword keywords[]{
    {"true", TokenKind::true_keyword}, {"false", TokenKind::false_keyword},
    {"none", TokenKind::null_keyword}, {"and", TokenKind::logical_and},
    {"or", TokenKind::logical_or},     {"not", TokenKind::logical_not},
    {"is", TokenKind::identity_is},    {"in", TokenKind::membership_in},
    {"if", TokenKind::conditional_if}, {"else", TokenKind::conditional_else},
};

constexpr ExpressionSymbol symbols[]{
    {"**", TokenKind::power},
    {"//", TokenKind::floor_slash},
    {"==", TokenKind::equal_equal},
    {"!=", TokenKind::not_equal},
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
    keywords,
    std::size(keywords),
    symbols,
    std::size(symbols),
    StringEscapeMode::backslash,
    false,
    false,
    false,
    false,
    false,
    false,
};

}  // namespace

LexerResult lex_python_expression(const std::string_view input, const std::size_t line,
                                  const std::size_t column) {
  return scan_expression(input, scanner_profile, line, column);
}

}  // namespace mpf::detail
