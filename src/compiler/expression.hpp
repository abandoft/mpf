#pragma once

#include <vector>

#include "expression_ast.hpp"
#include "lexer/lexer.hpp"
#include "mpf/diagnostic.hpp"
#include "mpf/transpiler.hpp"

namespace mpf::detail {

struct ExpressionParseResult {
  Expression expression;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] ExpressionParseResult parse_expression(LexerResult lexed, SourceLanguage language);

}  // namespace mpf::detail
