#include <iterator>
#include <utility>

#include "../compiler/frontend.hpp"
#include "../lexer/matlab_statement_lexer.hpp"
#include "common.hpp"
#include "frontend_descriptor.hpp"
#include "logical_source.hpp"
#include "matlab_statement_parser.hpp"

namespace mpf::detail {
namespace {

FrontendParseResult parse_matlab_descriptor(const SourceText& source,
                                            const FrontendParseOptions& options) {
  auto parsed = parse_matlab(source);
  auto ast = make_matlab_ast(std::move(parsed.program), options.memory_resource);
  return {FrontendAst{std::move(ast)}, std::move(parsed.diagnostics)};
}

hir::LoweringResult lower_matlab_ast(FrontendAst&& artifact) {
  auto* ast = std::get_if<matlab::ast::Program>(&artifact);
  if (ast == nullptr) {
    return {{},
            {{DiagnosticSeverity::error,
              "MPF0005",
              "Matlab lowering received another frontend's AST",
              {1, 1}}}};
  }
  return mpf::detail::lower_matlab_ast(std::move(*ast));
}

std::vector<Diagnostic> verify_matlab_ast(const FrontendAst& artifact) {
  return verify_frontend_ast(artifact, SourceLanguage::matlab);
}

int probe_matlab(const std::string_view source) noexcept {
  if (frontend::contains_ci(source, "disp(")) return 80;
  if (frontend::contains_ci(source, "function ")) return 70;
  if (source.find('%') != std::string_view::npos) return 50;
  return 0;
}

constexpr std::string_view aliases[]{"m"};
constexpr std::string_view extensions[]{".m"};
constexpr SourceIntrinsicBinding intrinsic_bindings[]{{"length", IntrinsicId::matlab_length},
                                                      {"numel", IntrinsicId::element_count},
                                                      {"reshape", IntrinsicId::reshape}};

}  // namespace

ParseResult parse_matlab(const SourceText& source) {
  auto normalized = normalize_matlab_source(source);
  auto lexed = lex_matlab_statements(std::move(normalized.lines));
  lexed.diagnostics.insert(lexed.diagnostics.begin(),
                           std::make_move_iterator(normalized.diagnostics.begin()),
                           std::make_move_iterator(normalized.diagnostics.end()));
  return parse_matlab_statements(std::move(lexed.lines), std::move(lexed.diagnostics));
}

const FrontendDescriptor& matlab_frontend() noexcept {
  static const SourceIntrinsicTable intrinsic_tables[]{
      mathematical_intrinsic_table(), {intrinsic_bindings, std::size(intrinsic_bindings)}};
  static const FrontendDescriptor descriptor{
      frontend_descriptor_api_version,
      SourceLanguage::matlab,
      "matlab",
      {aliases, std::size(aliases)},
      {extensions, std::size(extensions)},
      {"2024-and-earlier-supported-subset", "mpf.matlab.ast.v2", true, true},
      intrinsic_tables,
      std::size(intrinsic_tables),
      &parse_matlab_descriptor,
      &verify_matlab_ast,
      &lower_matlab_ast,
      &probe_matlab};
  return descriptor;
}

}  // namespace mpf::detail
