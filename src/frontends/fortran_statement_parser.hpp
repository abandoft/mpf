#pragma once

#include <vector>

#include "../compiler/ir.hpp"
#include "../lexer/fortran_statement_lexer.hpp"

namespace mpf::detail {

[[nodiscard]] ParseResult parse_fortran_statements(std::vector<FortranStatementLine> lines,
                                                   std::vector<Diagnostic> diagnostics = {});

}  // namespace mpf::detail
