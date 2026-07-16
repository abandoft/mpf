#pragma once

#include <memory_resource>
#include <vector>

#include "../lexer/matlab_statement_lexer.hpp"
#include "frontend_ast.hpp"

namespace mpf::detail {

[[nodiscard]] matlab::ast::ParseResult parse_matlab_statements(
    std::vector<MatlabStatementLine> lines, std::vector<Diagnostic> diagnostics = {},
    LanguageVersion version = {2024, 2},
    std::pmr::memory_resource* resource = std::pmr::get_default_resource());

}  // namespace mpf::detail
