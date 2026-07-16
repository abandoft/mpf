#include <iterator>
#include <utility>

#include "../compiler/frontend.hpp"
#include "../lexer/fortran_statement_lexer.hpp"
#include "common.hpp"
#include "fortran_source_form.hpp"
#include "fortran_statement_parser.hpp"
#include "frontend_descriptor.hpp"

namespace mpf::detail {
namespace {

FrontendParseResult parse_fortran_descriptor(const SourceText& source,
                                             const FrontendParseOptions& options) {
  auto parsed = parse_fortran(source, options.fortran_source_form);
  auto ast = make_fortran_ast(std::move(parsed.program), options.memory_resource);
  return {FrontendAst{std::move(ast)}, std::move(parsed.diagnostics)};
}

hir::LoweringResult lower_fortran_ast(FrontendAst&& artifact) {
  auto* ast = std::get_if<fortran::ast::Program>(&artifact);
  if (ast == nullptr) {
    return {{},
            {{DiagnosticSeverity::error,
              "MPF0005",
              "Fortran lowering received another frontend's AST",
              {1, 1}}}};
  }
  return mpf::detail::lower_fortran_ast(std::move(*ast));
}

std::vector<Diagnostic> verify_fortran_ast(const FrontendAst& artifact) {
  return verify_frontend_ast(artifact, SourceLanguage::fortran);
}

int probe_fortran(const std::string_view source) noexcept {
  if (frontend::contains_ci(source, "implicit none")) return 100;
  if (frontend::contains_ci(source, "end program")) return 95;
  if (frontend::starts_with_ci(source, "program ")) return 90;
  return 0;
}

constexpr std::string_view aliases[]{"f", "f90"};
constexpr std::string_view extensions[]{".f",   ".for", ".ftn", ".f77", ".f90",
                                        ".f95", ".f03", ".f08", ".f18", ".f23"};
constexpr SourceIntrinsicBinding intrinsic_bindings[]{{"present", IntrinsicId::present},
                                                      {"reshape", IntrinsicId::reshape},
                                                      {"size", IntrinsicId::element_count}};

}  // namespace

ParseResult parse_fortran(const SourceText& source, const FortranSourceForm source_form) {
  auto normalized = normalize_fortran_source(source, source_form);
  auto lexed = lex_fortran_statements(std::move(normalized.lines));
  lexed.diagnostics.insert(lexed.diagnostics.begin(),
                           std::make_move_iterator(normalized.diagnostics.begin()),
                           std::make_move_iterator(normalized.diagnostics.end()));
  return parse_fortran_statements(std::move(lexed.lines), std::move(lexed.diagnostics));
}

const FrontendDescriptor& fortran_frontend() noexcept {
  static const SourceIntrinsicTable intrinsic_tables[]{
      mathematical_intrinsic_table(), {intrinsic_bindings, std::size(intrinsic_bindings)}};
  static const FrontendDescriptor descriptor{
      frontend_descriptor_api_version,
      SourceLanguage::fortran,
      "fortran",
      {aliases, std::size(aliases)},
      {extensions, std::size(extensions)},
      {"2023-and-earlier-supported-subset", "mpf.fortran.ast.v2", true, true},
      intrinsic_tables,
      std::size(intrinsic_tables),
      &parse_fortran_descriptor,
      &verify_fortran_ast,
      &lower_fortran_ast,
      &probe_fortran};
  return descriptor;
}

}  // namespace mpf::detail
