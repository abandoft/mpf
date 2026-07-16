#pragma once

#include "../frontends/frontend_ast.hpp"
#include "ir.hpp"

namespace mpf::detail {

[[nodiscard]] FrontendParseResult parse_program(const SourceText& source, SourceLanguage language,
                                                FortranSourceForm fortran_source_form);
[[nodiscard]] ParseResult parse_python(const SourceText& source, LanguageVersion version = {3, 14});
[[nodiscard]] ParseResult parse_matlab(const SourceText& source,
                                       LanguageVersion version = {2024, 2});
[[nodiscard]] ParseResult parse_fortran(const SourceText& source, FortranSourceForm source_form,
                                        LanguageVersion version = {2023, 0});

}  // namespace mpf::detail
