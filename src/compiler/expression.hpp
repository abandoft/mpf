#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "expression_ast.hpp"
#include "mpf/diagnostic.hpp"
#include "mpf/transpiler.hpp"

namespace mpf::detail {

struct ExpressionParseResult {
  Expression expression;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] ExpressionParseResult parse_expression(std::string_view expression,
                                                     SourceLanguage language, std::size_t line,
                                                     std::size_t column = 1);

}  // namespace mpf::detail
