#pragma once

#include <memory_resource>

#include "../lexer/typescript_statement_lexer.hpp"
#include "frontend_ast.hpp"

namespace mpf::detail {

[[nodiscard]] typescript::ast::ParseResult parse_typescript_statements(
    TypeScriptStatementLexResult lexed, LanguageVersion version = {6, 0},
    std::pmr::memory_resource* resource = std::pmr::get_default_resource());

}  // namespace mpf::detail
