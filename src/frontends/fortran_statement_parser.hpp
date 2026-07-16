#pragma once

#include <memory_resource>
#include <vector>

#include "../lexer/fortran_statement_lexer.hpp"
#include "frontend_ast.hpp"

namespace mpf::detail {

[[nodiscard]] fortran::ast::ParseResult parse_fortran_statements(
    std::vector<FortranStatementLine> lines, std::vector<Diagnostic> diagnostics = {},
    LanguageVersion version = {2023, 0},
    std::pmr::memory_resource* resource = std::pmr::get_default_resource());

}  // namespace mpf::detail
