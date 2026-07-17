#pragma once

#include "lexer/lexer.hpp"

namespace mpf::detail {

[[nodiscard]] LexerResult lex_matlab_expression(std::string_view input, std::size_t line,
                                                std::size_t column);

}  // namespace mpf::detail
