#include <iterator>
#include <utility>

#include "../compiler/frontend.hpp"
#include "../lexer/python_statement_lexer.hpp"
#include "common.hpp"
#include "frontend_descriptor.hpp"
#include "logical_source.hpp"
#include "python_statement_parser.hpp"

namespace mpf::detail {
namespace {

FrontendParseResult parse_python_descriptor(const SourceText& source,
                                            const FrontendParseOptions& options) {
  const auto version =
      options.language_version.automatic() ? LanguageVersion{3, 14} : options.language_version;
  auto parsed = parse_python(source, version);
  auto ast = make_python_ast(std::move(parsed.program), options.memory_resource);
  return {FrontendAst{std::move(ast)}, std::move(parsed.diagnostics)};
}

hir::LoweringResult lower_python_ast(FrontendAst&& artifact) {
  auto* ast = std::get_if<python::ast::Program>(&artifact);
  if (ast == nullptr) {
    return {{},
            {{DiagnosticSeverity::error,
              "MPF0005",
              "Python lowering received another frontend's AST",
              {1, 1}}}};
  }
  return mpf::detail::lower_python_ast(std::move(*ast));
}

std::vector<Diagnostic> verify_python_ast(const FrontendAst& artifact) {
  return verify_frontend_ast(artifact, SourceLanguage::python);
}

int probe_python(const std::string_view source) noexcept {
  if (frontend::contains_ci(source, "def ")) return 90;
  if (frontend::contains_ci(source, "import ")) return 80;
  if (frontend::contains_ci(source, "print(")) return 60;
  return 0;
}

constexpr std::string_view aliases[]{"py"};
constexpr std::string_view extensions[]{".py", ".pyw"};
constexpr SourceIntrinsicBinding intrinsic_bindings[]{{"float", IntrinsicId::python_float},
                                                      {"len", IntrinsicId::python_length}};

}  // namespace

ParseResult parse_python(const SourceText& source, const LanguageVersion version) {
  auto normalized = normalize_python_source(source);
  auto lexed = lex_python_statements(std::move(normalized.lines));
  lexed.diagnostics.insert(lexed.diagnostics.begin(),
                           std::make_move_iterator(normalized.diagnostics.begin()),
                           std::make_move_iterator(normalized.diagnostics.end()));
  return parse_python_statements(std::move(lexed.lines), std::move(lexed.diagnostics), version);
}

const FrontendDescriptor& python_frontend() noexcept {
  static const SourceIntrinsicTable intrinsic_tables[]{
      mathematical_intrinsic_table(), {intrinsic_bindings, std::size(intrinsic_bindings)}};
  static const FrontendDescriptor descriptor{
      frontend_descriptor_api_version,
      SourceLanguage::python,
      "python",
      {aliases, std::size(aliases)},
      {extensions, std::size(extensions)},
      {"3.14-versioned-subset", "mpf.python.ast.v2", {3, 0}, {3, 14}, true, true},
      intrinsic_tables,
      std::size(intrinsic_tables),
      &parse_python_descriptor,
      &verify_python_ast,
      &lower_python_ast,
      &probe_python};
  return descriptor;
}

}  // namespace mpf::detail
