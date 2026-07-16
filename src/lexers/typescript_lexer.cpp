#include "../lexer/lexer.hpp"
#include "../lexer/scanner.hpp"

namespace mpf::detail {

LexerResult lex_typescript_expression(const std::string_view input, const std::size_t line,
                                      const std::size_t column) {
  return scan_expression(input, SourceLanguage::typescript, line, column);
}

}  // namespace mpf::detail
