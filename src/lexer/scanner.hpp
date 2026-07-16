#pragma once

#include "lexer.hpp"

namespace mpf::detail {

[[nodiscard]] LexerResult scan_expression(std::string_view input, SourceLanguage language,
                                          std::size_t line, std::size_t column);

}  // namespace mpf::detail
