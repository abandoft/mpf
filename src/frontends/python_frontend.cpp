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

class PythonParserSession final : public FrontendParserSession {
 public:
  explicit PythonParserSession(const FrontendParseOptions options) : options_(options) {}

  FrontendParseResult parse(const SourceText& source) override {
    const auto version =
        options_.language_version.automatic() ? LanguageVersion{3, 14} : options_.language_version;
    auto parsed = parse_python(source, version);
    auto ast = make_python_ast(std::move(parsed.program), options_.memory_resource);
    return {FrontendAst{std::move(ast)}, std::move(parsed.diagnostics)};
  }

 private:
  FrontendParseOptions options_;
};

std::unique_ptr<FrontendParserSession> create_python_parser_session(
    const FrontendParseOptions& options) {
  return std::make_unique<PythonParserSession>(options);
}

hir::LoweringResult lower_python_ast(FrontendAst&& artifact) {
  auto* ast = std::get_if<python::ast::Program>(&artifact);
  if (ast == nullptr) {
    return {{},
            {},
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
constexpr FrontendFeatureSet features{
    static_cast<std::uint64_t>(FrontendFeature::language_versioning) |
    static_cast<std::uint64_t>(FrontendFeature::structured_control_flow) |
    static_cast<std::uint64_t>(FrontendFeature::functions) |
    static_cast<std::uint64_t>(FrontendFeature::keyword_arguments) |
    static_cast<std::uint64_t>(FrontendFeature::multiple_results) |
    static_cast<std::uint64_t>(FrontendFeature::array_sections)};

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
  static const FrontendDescriptor descriptor{frontend_descriptor_api_version,
                                             SourceLanguage::python,
                                             "python",
                                             {aliases, std::size(aliases)},
                                             {extensions, std::size(extensions)},
                                             {"3.14-versioned-subset",
                                              "mpf.python.ast.v3",
                                              {3, 0},
                                              {3, 14},
                                              features,
                                              standard_frontend_resource_contract,
                                              true,
                                              true},
                                             intrinsic_tables,
                                             std::size(intrinsic_tables),
                                             &create_python_parser_session,
                                             &verify_python_ast,
                                             &lower_python_ast,
                                             &probe_python};
  return descriptor;
}

}  // namespace mpf::detail
