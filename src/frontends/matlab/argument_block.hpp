#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "compiler/argument_validation.hpp"
#include "frontends/matlab/statement_lexer.hpp"

namespace mpf::detail {

struct MatlabArgumentDeclaration {
  ArgumentDeclarationSyntax syntax;
  std::string default_source;
};

struct MatlabArgumentBlockParseResult {
  std::vector<MatlabArgumentDeclaration> declarations;
  std::vector<Diagnostic> diagnostics;
  std::size_t next_line{0};
  bool present{false};
};

[[nodiscard]] MatlabArgumentBlockParseResult parse_matlab_argument_blocks(
    const std::vector<MatlabStatementLine>& lines, std::size_t first_line,
    LanguageVersion version = {2024, 2});

}  // namespace mpf::detail
