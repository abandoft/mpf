#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "mpf/diagnostic.hpp"
#include "mpf/transpiler.hpp"
#include "token.hpp"

namespace mpf::detail {

struct LexerResult {
  std::vector<Token> tokens;
  std::vector<Diagnostic> diagnostics;
};

using ExpressionLexer = LexerResult (*)(std::string_view input, std::size_t line,
                                        std::size_t column);

}  // namespace mpf::detail
