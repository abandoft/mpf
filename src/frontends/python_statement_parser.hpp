#pragma once

#include <vector>

#include "../compiler/ir.hpp"
#include "../lexer/python_statement_lexer.hpp"

namespace mpf::detail {

[[nodiscard]] ParseResult parse_python_statements(std::vector<PythonStatementLine> lines,
                                                  std::vector<Diagnostic> diagnostics = {});

}  // namespace mpf::detail
