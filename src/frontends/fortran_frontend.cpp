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

class FortranParserSession final : public FrontendParserSession {
 public:
  explicit FortranParserSession(const FrontendParseOptions options) : options_(options) {}

  FrontendParseResult parse(const SourceText& source) override {
    const auto version = options_.language_version.automatic() ? LanguageVersion{2023, 0}
                                                               : options_.language_version;
    auto parsed = parse_fortran(source, options_.fortran_source_form, version);
    auto ast = make_fortran_ast(std::move(parsed.program), options_.memory_resource);
    return {FrontendAst{std::move(ast)}, std::move(parsed.diagnostics)};
  }

 private:
  FrontendParseOptions options_;
};

std::unique_ptr<FrontendParserSession> create_fortran_parser_session(
    const FrontendParseOptions& options) {
  return std::make_unique<FortranParserSession>(options);
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
constexpr FrontendFeatureSet features{
    static_cast<std::uint64_t>(FrontendFeature::language_versioning) |
    static_cast<std::uint64_t>(FrontendFeature::structured_control_flow) |
    static_cast<std::uint64_t>(FrontendFeature::functions) |
    static_cast<std::uint64_t>(FrontendFeature::keyword_arguments) |
    static_cast<std::uint64_t>(FrontendFeature::multiple_results) |
    static_cast<std::uint64_t>(FrontendFeature::array_sections) |
    static_cast<std::uint64_t>(FrontendFeature::fixed_source_form) |
    static_cast<std::uint64_t>(FrontendFeature::parameter_intent)};

}  // namespace

ParseResult parse_fortran(const SourceText& source, const FortranSourceForm source_form,
                          const LanguageVersion version) {
  auto normalized = normalize_fortran_source(source, source_form);
  auto lexed = lex_fortran_statements(std::move(normalized.lines));
  lexed.diagnostics.insert(lexed.diagnostics.begin(),
                           std::make_move_iterator(normalized.diagnostics.begin()),
                           std::make_move_iterator(normalized.diagnostics.end()));
  return parse_fortran_statements(std::move(lexed.lines), std::move(lexed.diagnostics), version);
}

const FrontendDescriptor& fortran_frontend() noexcept {
  static const SourceIntrinsicTable intrinsic_tables[]{
      mathematical_intrinsic_table(), {intrinsic_bindings, std::size(intrinsic_bindings)}};
  static const FrontendDescriptor descriptor{frontend_descriptor_api_version,
                                             SourceLanguage::fortran,
                                             "fortran",
                                             {aliases, std::size(aliases)},
                                             {extensions, std::size(extensions)},
                                             {"Fortran-2023-versioned-subset",
                                              "mpf.fortran.ast.v2",
                                              {77, 0},
                                              {2023, 0},
                                              features,
                                              standard_frontend_resource_contract,
                                              true,
                                              true},
                                             intrinsic_tables,
                                             std::size(intrinsic_tables),
                                             &create_fortran_parser_session,
                                             &verify_fortran_ast,
                                             &lower_fortran_ast,
                                             &probe_fortran};
  return descriptor;
}

}  // namespace mpf::detail
