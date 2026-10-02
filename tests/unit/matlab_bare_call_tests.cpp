#include <algorithm>
#include <string>

#include "frontends/common/registry.hpp"
#include "ir/mir.hpp"
#include "semantic/analyzer.hpp"
#include "semantic/matlab_bare_calls.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

hir::LoweringResult lower(const std::string& source) {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(source, "bare_calls.m"));
  REQUIRE(parsed.diagnostics.empty());
  auto result = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(result.diagnostics.empty());
  return result;
}

constexpr auto defaults =
    "checked;\nfunction output = checked(input)\narguments\n"
    "input (1,1) double = 42\nend\noutput = input;\nend\n";
}  // namespace

TEST_CASE("Matlab parser defers bare function invocation to lexical name binding") {
  auto lowered = lower(defaults);
  REQUIRE(lowered.program.statements.front().expression.kind == ExpressionKind::identifier);
  const auto names = analyze_names(lowered.program);
  REQUIRE(names.diagnostics.empty());
  REQUIRE(normalize_matlab_bare_calls(lowered.program, names.names));
  auto& statement = lowered.program.statements.front();
  REQUIRE(statement.expression.kind == ExpressionKind::call);
  REQUIRE(statement.expression.children.front().kind == ExpressionKind::identifier);
  REQUIRE(statement.expression.children.front().value == "checked");
  REQUIRE(statement.implicit_result == semantic::ImplicitResultPolicy::matlab_ans_if_value);
  REQUIRE(statement.name == "ans");
  REQUIRE(statement.expression.location.line ==
          statement.expression.children.front().location.line);
  REQUIRE(statement.expression.location.column ==
          statement.expression.children.front().location.column);
}

TEST_CASE("bare calls with default inputs reindex names flow and semantic demand together") {
  auto lowered = lower(defaults);
  const auto revision = lowered.program.revision;
  auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.diagnostics.empty());
  REQUIRE(lowered.program.revision > revision);
  REQUIRE(analysis.names.hir_revision == lowered.program.revision);
  REQUIRE(hir::verify_semantics(lowered.program, analysis.semantics, "bare-default").empty());
  REQUIRE(verify_names(lowered.program, analysis.names, "bare-default").empty());
  const auto& call = lowered.program.statements.front().expression;
  REQUIRE(call.children.size() == 2U);
  REQUIRE(call.children[1].kind == ExpressionKind::omitted_argument);
  REQUIRE(analysis.semantics.expression(call.id)->output_demand ==
          (SourceOutputDemand{OutputDemandForm::statement, 0U, true}));
  auto mir = mir::lower_from_hir(std::move(lowered.program), std::move(analysis.semantics),
                                 analysis.names);
  REQUIRE(mir.diagnostics.empty());
  REQUIRE(mir::verify(mir.program, "bare-default").empty());
}

TEST_CASE("unrelated function parameter spelling does not suppress a bound bare function call") {
  const std::string source =
      "ping;\nfunction output = wrapper(ping)\noutput = ping;\nend\n"
      "function ping\ndisp(7);\nend\n";
  auto lowered = lower(source);
  auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.diagnostics.empty());
  REQUIRE(lowered.program.statements.front().expression.kind == ExpressionKind::call);
  const auto& shadow = lowered.program.statements[1].body.front().expression;
  REQUIRE(shadow.kind == ExpressionKind::identifier);
  REQUIRE(analysis.semantics.expression(shadow.id)->binding == BindingKind::variable);
}

TEST_CASE(
    "Matlab bare function values in expressions become one-output calls but not call callees") {
  auto lowered = lower("disp(answer + answer());\nfunction output = answer()\noutput = 21;\nend\n");
  auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.diagnostics.empty());
  const auto& sum = lowered.program.statements.front().expression;
  REQUIRE(sum.children.size() == 2U);
  for (const auto& call : sum.children) {
    REQUIRE(call.kind == ExpressionKind::call);
    REQUIRE(call.children.size() == 1U);
    REQUIRE(call.children.front().kind == ExpressionKind::identifier);
    REQUIRE(analysis.semantics.expression(call.id)->output_demand ==
            (SourceOutputDemand{OutputDemandForm::expression, 1U, false}));
  }
  REQUIRE(lowered.program.statements.front().implicit_result ==
          semantic::ImplicitResultPolicy::none);
}

TEST_CASE(
    "bare local invocation with missing required inputs uses the ordinary call arity diagnostic") {
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::matlab;
    options.target = target;
    const auto result = mpf::Transpiler{}.transpile(
        "required;\nfunction output = required(input)\noutput = input;\nend\n", options);
    REQUIRE(!result.success());
    REQUIRE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                        [](const auto& value) { return value.code == "MPF2034"; }));
    REQUIRE(result.code.empty());
  }
}

TEST_CASE("bare variable references remain variables and normalization is a fixed point") {
  auto lowered = lower("ping = 42;\nping;\nfunction output = other(ping)\noutput = ping;\nend\n");
  auto names = analyze_names(lowered.program);
  REQUIRE(names.diagnostics.empty());
  REQUIRE(!normalize_matlab_bare_calls(lowered.program, names.names));
  REQUIRE(lowered.program.statements[1].expression.kind == ExpressionKind::identifier);
  auto calls = lower(defaults);
  const auto original_names = analyze_names(calls.program);
  REQUIRE(normalize_matlab_bare_calls(calls.program, original_names.names));
  ++calls.program.revision;
  calls.semantics = hir::reindex_semantics(calls.program, std::move(calls.semantics));
  const auto rebound = analyze_names(calls.program);
  REQUIRE(rebound.diagnostics.empty());
  REQUIRE(!normalize_matlab_bare_calls(calls.program, rebound.names));
}
