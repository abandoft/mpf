#pragma once

#include "../frontends/frontend_ast.hpp"
#include "ir.hpp"

namespace mpf::detail {

[[nodiscard]] FrontendParseResult parse_program(const SourceText& source, SourceLanguage language,
                                                FortranSourceForm fortran_source_form);
[[nodiscard]] ParseResult parse_python(const SourceText& source);
[[nodiscard]] ParseResult parse_matlab(const SourceText& source);
[[nodiscard]] ParseResult parse_fortran(const SourceText& source, FortranSourceForm source_form);

}  // namespace mpf::detail
