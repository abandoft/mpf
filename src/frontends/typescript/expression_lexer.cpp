#include "frontends/typescript/expression_lexer.hpp"

#include <iterator>

#include "lexer/scanner.hpp"

namespace mpf::detail {
namespace {

constexpr ExpressionKeyword keywords[]{
    {"true", TokenKind::true_keyword},
    {"false", TokenKind::false_keyword},
    {"null", TokenKind::null_keyword},
};

constexpr ExpressionSymbol symbols[]{
    {"===", TokenKind::equal_equal},
    {"!==", TokenKind::not_equal},
    {"**", TokenKind::power},
    {"==", TokenKind::equal_equal},
    {"!=", TokenKind::not_equal},
    {"<=", TokenKind::less_equal},
    {">=", TokenKind::greater_equal},
    {"&&", TokenKind::logical_and},
    {"||", TokenKind::logical_or},
    {"+", TokenKind::plus},
    {"-", TokenKind::minus},
    {"*", TokenKind::star},
    {"/", TokenKind::slash},
    {"%", TokenKind::percent},
    {"<", TokenKind::less},
    {">", TokenKind::greater},
    {"!", TokenKind::logical_not},
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
    true,
    false,
    false,
    false,
    false,
    false,
};

}  // namespace

LexerResult lex_typescript_expression(const std::string_view input, const std::size_t line,
                                      const std::size_t column) {
  return scan_expression(input, scanner_profile, line, column);
}

}  // namespace mpf::detail
