#include "../lexer/scanner.hpp"

namespace mpf::detail {

LexerResult lex_fortran_expression(const std::string_view input, const std::size_t line,
                                   const std::size_t column) {
  return scan_expression(input, SourceLanguage::fortran, line, column);
}

}  // namespace mpf::detail
