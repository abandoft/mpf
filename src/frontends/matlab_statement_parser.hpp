#pragma once

#include <vector>

#include "../compiler/ir.hpp"
#include "../lexer/matlab_statement_lexer.hpp"

namespace mpf::detail {

[[nodiscard]] ParseResult parse_matlab_statements(std::vector<MatlabStatementLine> lines,
                                                  std::vector<Diagnostic> diagnostics = {});

}  // namespace mpf::detail
