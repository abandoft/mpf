#include <utility>

#include "frontends/common/descriptor.hpp"
#include "frontends/common/parser_support.hpp"
#include "frontends/typescript/statement_lexer.hpp"
#include "frontends/typescript/statement_parser.hpp"

namespace mpf::detail {
namespace {

class TypeScriptParserSession final : public FrontendParserSession {
 public:
  explicit TypeScriptParserSession(const FrontendParseOptions options) : options_(options) {}

  FrontendParseResult parse(const SourceText& source) override {
    const auto version =
        options_.language_version.automatic() ? LanguageVersion{6, 0} : options_.language_version;
    auto parsed = parse_typescript_statements(lex_typescript_statements(source), version,
                                              options_.memory_resource);
    return {FrontendAst{std::move(parsed.program)}, std::move(parsed.diagnostics)};
  }

 private:
  FrontendParseOptions options_;
};

std::unique_ptr<FrontendParserSession> create_typescript_parser_session(
    const FrontendParseOptions& options) {
  return std::make_unique<TypeScriptParserSession>(options);
}

hir::LoweringResult lower_typescript_ast(FrontendAst&& artifact) {
  auto* ast = std::get_if<typescript::ast::Program>(&artifact);
  if (ast == nullptr) {
    return {{},
            {},
            {{DiagnosticSeverity::error,
              "MPF0005",
              "TypeScript lowering received another frontend's AST",
              {1, 1}}}};
  }
  return mpf::detail::lower_typescript_ast(std::move(*ast));
}

std::vector<Diagnostic> verify_typescript_ast(const FrontendAst& artifact) {
  return verify_frontend_ast(artifact, SourceLanguage::typescript);
}

int probe_typescript(const std::string_view source) noexcept {
  if (frontend::contains_ci(source, "interface ") || frontend::contains_ci(source, "type ")) {
    return 95;
  }
  if (frontend::contains_ci(source, "function ") &&
      (frontend::contains_ci(source, ": number") || frontend::contains_ci(source, ": string") ||
       frontend::contains_ci(source, ": boolean"))) {
    return 90;
  }
  if (frontend::contains_ci(source, "console.log(")) return 70;
  if (frontend::contains_ci(source, "const ") || frontend::contains_ci(source, "let ")) return 40;
  return 0;
}

constexpr std::string_view extensions[]{".ts", ".mts", ".cts"};
constexpr FrontendFeatureSet features{
    static_cast<std::uint64_t>(FrontendFeature::language_versioning) |
    static_cast<std::uint64_t>(FrontendFeature::structured_control_flow) |
    static_cast<std::uint64_t>(FrontendFeature::functions)};

}  // namespace

const FrontendDescriptor& typescript_frontend() noexcept {
  static const FrontendDescriptor descriptor{frontend_descriptor_api_version,
                                             SourceLanguage::typescript,
                                             "typescript",
                                             {extensions, std::size(extensions)},
                                             {"6.0-versioned-subset",
                                              "mpf.typescript.ast.v1",
                                              {1, 0},
                                              {6, 0},
                                              features,
                                              standard_frontend_resource_contract,
                                              true,
                                              true},
                                             nullptr,
                                             0,
                                             &create_typescript_parser_session,
                                             &verify_typescript_ast,
                                             &lower_typescript_ast,
                                             &probe_typescript};
  return descriptor;
}

}  // namespace mpf::detail
