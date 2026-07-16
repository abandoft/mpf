#include "../lexer/scanner.hpp"

namespace mpf::detail {

LexerResult lex_python_expression(const std::string_view input, const std::size_t line,
                                  const std::size_t column) {
  return scan_expression(input, SourceLanguage::python, line, column);
}

}  // namespace mpf::detail
