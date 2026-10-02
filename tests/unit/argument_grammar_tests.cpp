#include <algorithm>
#include <string>

#include "compiler/argument_validator_catalog.hpp"
#include "frontends/common/registry.hpp"
#include "ir/dump.hpp"
#include "ir/mir.hpp"
#include "semantic/analyzer.hpp"
#include "semantic/function_dependencies.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

std::string function_source(const std::string& validators) {
  return "function output = checked(lower,value)\narguments\nlower (1,1) double\n"
         "value (1,1) double {" +
         validators + "}\nend\noutput = value\nend\n";
}

FrontendParseResult parse(const std::string& source,
                          const mpf::LanguageVersion version = {2024, 2}) {
  FrontendParseOptions options;
  options.language_version = version;
  auto result =
      parse_with_frontend(matlab_frontend(), SourceText(source, "validator_grammar.m"), options);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(matlab_frontend().verify(result.ast).empty());
  return result;
}

bool diagnostic(const std::vector<mpf::Diagnostic>& diagnostics, const std::string& code) {
  return std::any_of(diagnostics.begin(), diagnostics.end(),
                     [&](const auto& entry) { return entry.code == code; });
}

mpf::TranspileResult compile(const std::string& source, const mpf::TargetLanguage target,
                             const mpf::LanguageVersion version = {2024, 2}) {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::matlab;
  options.target = target;
  options.language_version = version;
  return mpf::Transpiler{}.transpile(source, options);
}
}  // namespace

TEST_CASE("validator grammar owns unknown named calls and general ordered argument expressions") {
  auto parsed =
      parse(function_source("custom, custom(value,lower + 1,[1,2],complex(1,2),'text'), zero()"));
  auto lowered = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  const auto& function = lowered.program.statements.front();
  const auto& syntax = function.argument_declarations[1].validators;
  REQUIRE(syntax.size() == 3U);
  REQUIRE(syntax[0].name == "custom");
  REQUIRE(!syntax[0].explicit_call);
  REQUIRE(syntax[1].argument_count == 5U);
  REQUIRE(syntax[1].explicit_call);
  REQUIRE(syntax[2].argument_count == 0U);
  const auto& calls = function.argument_validator_calls;
  REQUIRE(calls.size() == 3U);
  REQUIRE(calls[0].expression.children[1].value == "value");
  REQUIRE(calls[1].expression.children[2].kind == ExpressionKind::binary);
  REQUIRE(calls[1].expression.children[3].children.size() == 2U);
  REQUIRE(calls[1].expression.children[4].kind == ExpressionKind::call);
  REQUIRE(calls[2].expression.children.size() == 1U);
}

TEST_CASE("validator parsing does not apply builtin version arity or threshold ABI rules") {
  for (const auto* call : {"mustBeFloat(value,lower + 1)", "mustBeRow(value,lower)",
                           "mustBeInRange(value,[0],lower + 1,'custom')", "mustBeReal()"}) {
    auto parsed = parse(function_source(call), {2019, 2});
    auto lowered = matlab_frontend().lower(std::move(parsed.ast));
    REQUIRE(lowered.diagnostics.empty());
    const auto names = analyze_names(lowered.program);
    REQUIRE(names.diagnostics.empty());
    const auto& callee = lowered.program.statements.front()
                             .argument_validator_calls.front()
                             .expression.children.front();
    const auto* use = names.names.reference(callee.id);
    REQUIRE(use != nullptr);
    REQUIRE(use->argument_validator.has_value());
  }
}

TEST_CASE("local validators bypass same-spelling builtin version and arity restrictions") {
  for (const auto* name : {"mustBeFloat", "mustBeRow", "mustBeInRange", "customValidator"}) {
    const auto source =
        function_source(std::string(name) + "(value,lower + 1)") + "function " + name +
        "(value,threshold)\nif value < threshold\nerror('invalid value')\nend\nend\n";
    auto parsed = parse(source, {2019, 2});
    auto lowered = matlab_frontend().lower(std::move(parsed.ast));
    REQUIRE(lowered.diagnostics.empty());
    const auto names = analyze_names(lowered.program);
    REQUIRE(names.diagnostics.empty());
    const auto& callee = lowered.program.statements.front()
                             .argument_validator_calls.front()
                             .expression.children.front();
    REQUIRE(names.names.reference(callee.id)->symbol.valid());
    REQUIRE(!names.names.reference(callee.id)->argument_validator.has_value());
    const auto graph = analyze_function_dependencies(lowered.program, names.names);
    REQUIRE(graph.dependencies[0] == std::vector<std::size_t>{1U});
    for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
      const auto result = compile(source, target, {2019, 2});
      REQUIRE(!result.success());
      REQUIRE(result.code.empty());
      REQUIRE(diagnostic(result.diagnostics, "MPF2062"));
      REQUIRE(!diagnostic(result.diagnostics, "MPF1200"));
      REQUIRE(!diagnostic(result.diagnostics, "MPF1201"));
      REQUIRE(!diagnostic(result.diagnostics, "MPF2060"));
      REQUIRE(!diagnostic(result.diagnostics, "MPF0005"));
    }
  }
}

TEST_CASE("standard validator version and call ABI checks run after successful binding") {
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto old = compile(function_source("mustBeFloat"), target, {2019, 2});
    REQUIRE(diagnostic(old.diagnostics, "MPF1201"));
    REQUIRE(!diagnostic(old.diagnostics, "MPF0005"));
    for (const auto* call :
         {"mustBeReal()", "mustBeReal(value,lower)", "mustBeGreaterThan(value,lower + 1)",
          "mustBeInRange(value,0,1,'custom')"}) {
      const auto invalid = compile(function_source(call), target);
      REQUIRE(diagnostic(invalid.diagnostics, "MPF2060"));
      REQUIRE(!diagnostic(invalid.diagnostics, "MPF1200"));
      REQUIRE(!diagnostic(invalid.diagnostics, "MPF0005"));
      REQUIRE(invalid.code.empty());
    }
  }
}

TEST_CASE("validator grammar rejects empty arguments and malformed declaration delimiters") {
  for (const auto* call : {"custom(value,,lower)", "custom(value,)", "custom(,value)",
                           "custom(value,[1,2)", "custom(value),", "custom(value) custom(value)"}) {
    const auto source = function_source(call);
    auto parsed = parse_with_frontend(matlab_frontend(), SourceText(source, "invalid_validator.m"));
    REQUIRE(!parsed.diagnostics.empty());
    REQUIRE(diagnostic(parsed.diagnostics, "MPF1200"));
  }
}

TEST_CASE("validator reindexing retains valid builtin plans across rejected source-call gaps") {
  const auto source =
      function_source(
          "customValidator(value,lower + 1), mustBeFinite, mustBeGreaterThan(value,lower)") +
      "function customValidator(value,threshold)\nif value < "
      "threshold\nerror('invalid')\nend\nend\n";
  auto parsed = parse(source);
  auto lowered = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(diagnostic(analysis.diagnostics, "MPF2062"));
  REQUIRE(!diagnostic(analysis.diagnostics, "MPF0005"));
  ++lowered.program.revision;
  analysis.semantics = hir::reindex_semantics(lowered.program, std::move(analysis.semantics));
  REQUIRE(
      hir::verify_semantics(lowered.program, analysis.semantics, "validator-gap-reindex").empty());
  const auto& function = lowered.program.statements.front();
  const auto& validators =
      analysis.semantics.statement(function.id)->argument_validations[1].validators;
  REQUIRE(validators.size() == 2U);
  REQUIRE(validators[0].source_call == function.argument_validator_calls[1].expression.id);
  REQUIRE(validators[1].source_call == function.argument_validator_calls[2].expression.id);
}

TEST_CASE("semantic verifier rejects validator plans whose source version or AST operands change") {
  for (const auto mutation : {0, 1, 2}) {
    auto parsed = parse(function_source("mustBeFloat, mustBeGreaterThan(value,1)"));
    auto lowered = matlab_frontend().lower(std::move(parsed.ast));
    auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
    REQUIRE(analysis.empty());
    auto& call = lowered.program.statements.front().argument_validator_calls[1].expression;
    if (mutation == 0) lowered.program.semantics.language_version = {2019, 2};
    if (mutation == 1) call.children[2].value = "2";
    if (mutation == 2) call.children[1].value = "lower";
    REQUIRE(
        !hir::verify_semantics(lowered.program, analysis.semantics, "validator-source-corruption")
             .empty());
  }
}

TEST_CASE("all frontend source versions survive arena lowering and typed MIR publication") {
  struct Case {
    const FrontendDescriptor* frontend;
    mpf::LanguageVersion version;
    const char* source;
  };
  for (const auto& fixture : {Case{&matlab_frontend(), {2019, 2}, "value = 1\n"},
                              Case{&python_frontend(), {3, 10}, "value = 1\n"},
                              Case{&fortran_frontend(),
                                   {2018, 0},
                                   "program demo\ninteger :: value\nvalue = 1\nend program demo\n"},
                              Case{&typescript_frontend(), {5, 0}, "const value = 1;\n"}}) {
    FrontendParseOptions options;
    options.language_version = fixture.version;
    auto parsed = parse_with_frontend(*fixture.frontend,
                                      SourceText(fixture.source, "version_source"), options);
    REQUIRE(parsed.diagnostics.empty());
    REQUIRE(fixture.frontend->verify(parsed.ast).empty());
    auto lowered = fixture.frontend->lower(std::move(parsed.ast));
    REQUIRE(lowered.diagnostics.empty());
    REQUIRE(lowered.program.semantics.language_version == fixture.version);
    auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
    REQUIRE(analysis.empty());
    auto middle = mir::lower_from_hir(std::move(lowered.program), std::move(analysis.semantics),
                                      analysis.names);
    REQUIRE(middle.diagnostics.empty());
    REQUIRE(middle.program.semantics.language_version == fixture.version);
    REQUIRE(dump_mir(middle.program).find("version=") != std::string::npos);
  }
}

TEST_CASE("MIR rejects standard validators unavailable in its own retained source version") {
  for (const auto* call : {"mustBeFloat", "mustBeRow", "mustBeInRange(value,0,1)"}) {
    auto parsed = parse(function_source(call));
    auto lowered = matlab_frontend().lower(std::move(parsed.ast));
    auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
    REQUIRE(analysis.empty());
    auto middle = mir::lower_from_hir(std::move(lowered.program), std::move(analysis.semantics),
                                      analysis.names);
    REQUIRE(middle.diagnostics.empty());
    middle.program.semantics.language_version = {2019, 2};
    const auto errors = mir::verify(middle.program, "validator-version-corruption");
    REQUIRE(!errors.empty());
    REQUIRE(std::any_of(errors.begin(), errors.end(), [](const auto& entry) {
      return entry.message.find("validator source binding inventory") != std::string::npos;
    }));
  }
}

TEST_CASE("validator catalog is a bijective immutable source definition index") {
  for (std::uint8_t index = 0U; index <= static_cast<std::uint8_t>(ArgumentValidator::in_range);
       ++index) {
    const auto validator = static_cast<ArgumentValidator>(index);
    const auto spelling = argument_validator_name(validator);
    const auto* definition = find_argument_validator(spelling);
    REQUIRE(definition != nullptr);
    REQUIRE(definition->validator == validator);
    REQUIRE(find_argument_validator(validator) == definition);
    REQUIRE(!definition->minimum_release.empty());
    REQUIRE(!definition->minimum_version.automatic());
  }
  REQUIRE(find_argument_validator("unknownValidator") == nullptr);
  REQUIRE(find_argument_validator("") == nullptr);
  REQUIRE(find_argument_validator("mustbefloat") == nullptr);
}
