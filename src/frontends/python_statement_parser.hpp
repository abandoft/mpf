#pragma once

#include <memory_resource>
#include <vector>

#include "../lexer/python_statement_lexer.hpp"
#include "frontend_ast.hpp"

namespace mpf::detail {

[[nodiscard]] python::ast::ParseResult parse_python_statements(
    std::vector<PythonStatementLine> lines, std::vector<Diagnostic> diagnostics = {},
    LanguageVersion version = {3, 14},
    std::pmr::memory_resource* resource = std::pmr::get_default_resource());

}  // namespace mpf::detail
