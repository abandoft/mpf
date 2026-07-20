#include <iterator>
#include <utility>

#include "frontends/common/descriptor.hpp"
#include "frontends/common/parser_support.hpp"
#include "frontends/matlab/logical_source.hpp"
#include "frontends/matlab/statement_lexer.hpp"
#include "frontends/matlab/statement_parser.hpp"

namespace mpf::detail {
namespace {

class MatlabParserSession final : public FrontendParserSession {
 public:
  explicit MatlabParserSession(const FrontendParseOptions options) : options_(options) {}

  FrontendParseResult parse(const SourceText& source) override {
    const auto version = options_.language_version.automatic() ? LanguageVersion{2024, 2}
                                                               : options_.language_version;
    auto normalized = normalize_matlab_source(source);
    auto lexed = lex_matlab_statements(std::move(normalized.lines));
    lexed.diagnostics.insert(lexed.diagnostics.begin(),
                             std::make_move_iterator(normalized.diagnostics.begin()),
                             std::make_move_iterator(normalized.diagnostics.end()));
    auto parsed = parse_matlab_statements(std::move(lexed.lines), std::move(lexed.diagnostics),
                                          version, options_.memory_resource);
    return {FrontendAst{std::move(parsed.program)}, std::move(parsed.diagnostics)};
  }

 private:
  FrontendParseOptions options_;
};

std::unique_ptr<FrontendParserSession> create_matlab_parser_session(
    const FrontendParseOptions& options) {
  return std::make_unique<MatlabParserSession>(options);
}

hir::LoweringResult lower_matlab_ast(FrontendAst&& artifact) {
  auto* ast = std::get_if<matlab::ast::Program>(&artifact);
  if (ast == nullptr) {
    return {{},
            {},
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

constexpr std::string_view extensions[]{".m"};
constexpr SourceIntrinsicBinding intrinsic_bindings[]{
    {"all", IntrinsicId::logical_all},           {"any", IntrinsicId::logical_any},
    {"complex", IntrinsicId::complex_value},     {"conj", IntrinsicId::conjugate},
    {"error", IntrinsicId::matlab_error},        {"full", IntrinsicId::matlab_full},
    {"i", IntrinsicId::imaginary_unit},          {"imag", IntrinsicId::imaginary_part},
    {"issparse", IntrinsicId::matlab_is_sparse}, {"j", IntrinsicId::imaginary_unit},
    {"length", IntrinsicId::matlab_length},      {"nnz", IntrinsicId::matlab_nonzero_count},
    {"numel", IntrinsicId::element_count},       {"real", IntrinsicId::real_part},
    {"reshape", IntrinsicId::reshape},           {"rethrow", IntrinsicId::matlab_rethrow},
    {"sparse", IntrinsicId::matlab_sparse}};
constexpr FrontendFeatureSet features{
    static_cast<std::uint64_t>(FrontendFeature::language_versioning) |
    static_cast<std::uint64_t>(FrontendFeature::structured_control_flow) |
    static_cast<std::uint64_t>(FrontendFeature::functions) |
    static_cast<std::uint64_t>(FrontendFeature::multiple_results) |
    static_cast<std::uint64_t>(FrontendFeature::array_sections) |
    static_cast<std::uint64_t>(FrontendFeature::exception_handling)};

}  // namespace

const FrontendDescriptor& matlab_frontend() noexcept {
  static const SourceIntrinsicTable intrinsic_tables[]{
      mathematical_intrinsic_table(), {intrinsic_bindings, std::size(intrinsic_bindings)}};
  static const FrontendDescriptor descriptor{frontend_descriptor_api_version,
                                             SourceLanguage::matlab,
                                             "matlab",
                                             {extensions, std::size(extensions)},
                                             {"Matlab-2024-versioned-subset",
                                              "mpf.matlab.ast.v4",
                                              {1, 0},
                                              {2024, 2},
                                              features,
                                              standard_frontend_resource_contract,
                                              true,
                                              true},
                                             intrinsic_tables,
                                             std::size(intrinsic_tables),
                                             &create_matlab_parser_session,
                                             &verify_matlab_ast,
                                             &lower_matlab_ast,
                                             &probe_matlab};
  return descriptor;
}

}  // namespace mpf::detail
