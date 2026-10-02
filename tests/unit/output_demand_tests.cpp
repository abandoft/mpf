#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "backends/common/lir_builder.hpp"
#include "backends/common/lir_dump.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "ir/dump.hpp"
#include "ir/mir_optimization.hpp"
#include "ir/mir_output_demand.hpp"
#include "mpf/transpiler.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

const std::string source =
    "checked();\nchecked\nvalue = checked();\n[first,second] = checked();\n"
    "disp(checked());\n"
    "function output = with_default(input)\narguments\n"
    "input (1,1) double = checked()\nend\noutput = input;\nend\n"
    "function [first,second] = checked()\narguments (Output)\n"
    "first (1,1) double {mustBePositive}\nsecond (1,1) logical {mustBeNonzero}\nend\n"
    "first = 4;\nsecond = 2;\nend\n";

struct Analyzed {
  hir::Program program;
  AnalysisResult analysis;
};

Analyzed analyze() {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(source, "demand.m"));
  REQUIRE(parsed.diagnostics.empty());
  auto lowered = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.diagnostics.empty());
  REQUIRE(hir::verify_semantics(lowered.program, analysis.semantics, "output-demand").empty());
  return {std::move(lowered.program), std::move(analysis)};
}

mir::Program lower() {
  auto analyzed = analyze();
  auto result = mir::lower_from_hir(
      std::move(analyzed.program), std::move(analyzed.analysis.semantics), analyzed.analysis.names);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(mir::verify(result.program, "output-demand").empty());
  return std::move(result.program);
}

template <typename Program>
auto& checked(Program& program) {
  const auto found =
      std::find_if(program.statements.begin(), program.statements.end(), [](const auto& statement) {
        return statement.kind == StatementKind::function && statement.name == "checked";
      });
  REQUIRE(found != program.statements.end());
  REQUIRE(!found->argument_validator_sources.empty());
  return *found;
}

template <typename Program, typename Statement, typename Expression, typename Selector>
Program project(const mir::Program& program) {
  auto result = lower_structured_lir<Program, Statement, Expression, Selector>(
      program, [](HirNodeId, IntrinsicId) { return CodeBinding{}; });
  return std::move(*result);
}

javascript::lir::SemanticProgram javascript_plan(const mir::Program& program) {
  auto result = project<javascript::lir::SemanticProgram, javascript::lir::Statement,
                        javascript::lir::Expression, javascript::lir::CaseSelector>(program);
  result.runtime.require(javascript::lir::RuntimeFeature::argument_validation);
  result.runtime.require(javascript::lir::RuntimeFeature::arrays);
  result.runtime.require(javascript::lir::RuntimeFeature::complex_numbers);
  javascript::plan_lir_resources(result, mpf::TranspileOptions{});
  javascript::plan_lir_representation(result);
  std::vector<mpf::Diagnostic> diagnostics;
  javascript::verify_lir_representation(result, diagnostics);
  REQUIRE(diagnostics.empty());
  return result;
}

cpp::lir::SemanticProgram cpp_plan(const mir::Program& program) {
  auto result = project<cpp::lir::SemanticProgram, cpp::lir::Statement, cpp::lir::Expression,
                        cpp::lir::CaseSelector>(program);
  result.runtime.require(cpp::lir::RuntimeFeature::argument_validation);
  cpp::plan_lir_resources(result, mpf::TranspileOptions{});
  cpp::plan_lir_representation(result);
  std::vector<mpf::Diagnostic> diagnostics;
  cpp::verify_lir_representation(result, diagnostics);
  REQUIRE(diagnostics.empty());
  return result;
}

template <typename Program, typename Plan, typename Verify>
void reject_target_corruption(const Program& pristine, Plan plan, Verify verify) {
  for (const auto mutation : {0, 1, 2, 3, 4, 5}) {
    auto program = pristine;
    auto& expression = program.statements.front().expression;
    if (mutation == 0) expression.plan.output_invocation.count = 1U;
    if (mutation == 1) expression.plan.output_invocation.implicit_result = true;
    if (mutation == 2) expression.output_demand = {};
    if (mutation == 3) {
      expression.output_demand = {OutputDemandForm::expression, 1U, false};
      plan(program);
    }
    if (mutation == 4) {
      program.statements[3].expression.output_demand.count = 1U;
      plan(program);
    }
    if (mutation == 5) {
      auto& literal = program.statements.back().body.front().expression;
      literal.output_demand = {OutputDemandForm::statement, 0U, false};
      plan(program);
    }
    std::vector<mpf::Diagnostic> diagnostics;
    verify(program, diagnostics);
    REQUIRE(!diagnostics.empty());
  }
}
}  // namespace

TEST_CASE("source output demand is a validated cardinality independent of target plan types") {
  const std::vector<SourceOutputDemand> valid{{},
                                              {OutputDemandForm::expression, 1U, false},
                                              {OutputDemandForm::statement, 0U, false},
                                              {OutputDemandForm::statement, 0U, true},
                                              {OutputDemandForm::prefix, 2U, false},
                                              {OutputDemandForm::validation, 0U, false}};
  for (const auto& demand : valid) REQUIRE(demand.valid());
  const std::vector<SourceOutputDemand> invalid{{OutputDemandForm::none, 1U, false},
                                                {OutputDemandForm::expression, 0U, false},
                                                {OutputDemandForm::statement, 1U, false},
                                                {OutputDemandForm::prefix, 0U, false},
                                                {OutputDemandForm::validation, 1U, false},
                                                {OutputDemandForm::expression, 1U, true},
                                                {static_cast<OutputDemandForm>(255U), 0U, false}};
  for (const auto& demand : invalid) REQUIRE(!demand.valid());
  static_assert(
      !std::is_same_v<cpp::lir::OutputInvocationPlan, javascript::lir::OutputInvocationPlan>);
  static_assert(
      !std::is_same_v<cpp::lir::OutputInvocationForm, javascript::lir::OutputInvocationForm>);
}

TEST_CASE("HIR output demand preserves statement prefix default and validator ownership") {
  const auto analyzed = analyze();
  const auto& statements = analyzed.program.statements;
  const auto demand = [&](const hir::Expression& expression) {
    return analyzed.analysis.semantics.expression(expression.id)->output_demand;
  };
  REQUIRE(demand(statements[0].expression) ==
          SourceOutputDemand({OutputDemandForm::statement, 0U, false}));
  REQUIRE(demand(statements[1].expression) ==
          SourceOutputDemand({OutputDemandForm::statement, 0U, true}));
  REQUIRE(demand(statements[2].expression) ==
          SourceOutputDemand({OutputDemandForm::expression, 1U, false}));
  REQUIRE(demand(statements[3].expression) ==
          SourceOutputDemand({OutputDemandForm::prefix, 2U, false}));
  REQUIRE(demand(statements[4].expression) ==
          SourceOutputDemand({OutputDemandForm::expression, 1U, false}));
  REQUIRE(demand(statements[5].parameter_defaults.front()) ==
          SourceOutputDemand({OutputDemandForm::expression, 1U, false}));
  for (const auto& call : statements.back().argument_validator_calls)
    REQUIRE(demand(call.expression) ==
            SourceOutputDemand({OutputDemandForm::validation, 0U, false}));
  REQUIRE(dump_semantics(analyzed.analysis.semantics).find("demand=2:0:1") != std::string::npos);
}

TEST_CASE(
    "MIR retains zero source demand separately from first selected values through optimization") {
  auto program = lower();
  REQUIRE(program.calls.size() == 6U);
  REQUIRE(program.calls[0].output_demand.count == 0U);
  REQUIRE(program.calls[0].requested_results == 1U);
  REQUIRE(!program.calls[0].output_demand.implicit_result);
  REQUIRE(program.calls[1].output_demand.count == 0U);
  REQUIRE(program.calls[1].output_demand.implicit_result);
  REQUIRE(program.calls[3].output_demand.count == 2U);
  REQUIRE(program.calls[3].requested_results == 2U);
  for (const auto& source_call : checked(program).argument_validator_sources)
    REQUIRE(source_call.output_demand ==
            SourceOutputDemand({OutputDemandForm::validation, 0U, false}));
  const auto before = program.calls;
  REQUIRE(mir::run_default_optimization_pipeline(program).diagnostics.empty());
  REQUIRE(mir::verify(program, "optimized-output-demand").empty());
  for (std::size_t index = 0U; index < before.size(); ++index)
    REQUIRE(program.calls[index].output_demand == before[index].output_demand);
  REQUIRE(dump_mir(program).find("requested=1 demand=2:0:0") != std::string::npos);
}

TEST_CASE("HIR independently rejects output demand forged to match another source context") {
  for (const auto mutation : {0, 1, 2, 3}) {
    auto analyzed = analyze();
    const auto& statements = analyzed.program.statements;
    if (mutation == 0)
      analyzed.analysis.semantics.expression(statements[0].expression.id)->output_demand = {
          OutputDemandForm::expression, 1U, false};
    if (mutation == 1)
      analyzed.analysis.semantics.expression(statements[3].expression.id)->output_demand.count = 1U;
    if (mutation == 2)
      analyzed.analysis.semantics.expression(statements[5].parameter_defaults[0].id)
          ->output_demand = {OutputDemandForm::statement, 0U, false};
    if (mutation == 3)
      analyzed.analysis.semantics
          .expression(statements.back().argument_validator_calls[0].expression.id)
          ->output_demand = {OutputDemandForm::expression, 1U, false};
    REQUIRE(!hir::verify_semantics(analyzed.program, analyzed.analysis.semantics, "forged-demand")
                 .empty());
  }
}

TEST_CASE("MIR independently rejects mirrored and orphan source output demand") {
  for (const auto mutation : {0, 1, 2, 3}) {
    auto program = lower();
    auto& call = program.calls.front();
    const auto expression = program.statements[1].expression;
    if (mutation == 0) call.output_demand = {OutputDemandForm::expression, 1U, false};
    if (mutation == 1) {
      call.output_demand = {OutputDemandForm::expression, 1U, false};
      mir::attributes(program, expression)->output_demand = call.output_demand;
    }
    if (mutation == 2)
      mir::attributes(program, program.statements[4].expression)->output_demand.count = 1U;
    if (mutation == 3) checked(program).argument_validator_sources.front().output_demand = {};
    REQUIRE(!mir::verify(program, "forged-demand").empty());
  }
}

TEST_CASE(
    "both target plans own zero demand and retain first results only for implicit receivers") {
  const auto program = lower();
  const auto javascript = javascript_plan(program);
  const auto cpp = cpp_plan(program);
  REQUIRE(javascript.statements[0].expression.plan.call_value ==
          javascript::lir::CallValueForm::discarded_result);
  REQUIRE(cpp.statements[0].expression.plan.call_value ==
          cpp::lir::CallValueForm::discarded_result);
  REQUIRE(javascript.statements[1].expression.plan.call_value ==
          javascript::lir::CallValueForm::first_result);
  REQUIRE(cpp.statements[1].expression.plan.call_value ==
          cpp::lir::CallValueForm::first_tuple_result);
  REQUIRE(javascript.statements[0].expression.plan.output_invocation.count == 0U);
  REQUIRE(cpp.statements[0].expression.plan.output_invocation.count == 0U);
  REQUIRE(javascript.statements[3].expression.plan.output_invocation.count == 2U);
  REQUIRE(cpp.statements[3].expression.plan.output_invocation.count == 2U);
}

TEST_CASE("target demand verifiers reject corrupt mirrors even after rebuilding private plans") {
  const auto program = lower();
  reject_target_corruption(javascript_plan(program), javascript::plan_lir_representation,
                           javascript::verify_lir_representation);
  reject_target_corruption(cpp_plan(program), cpp::plan_lir_representation,
                           cpp::verify_lir_representation);
}

TEST_CASE(
    "discarded multi-output calls avoid projections while retaining execution and source maps") {
  const auto body = source.substr(source.find("function [first,second]"));
  const std::string discarded = "checked();\n" + body;
  const std::string implicit = "checked\n" + body;
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::matlab;
    options.target = target;
    options.emit_source_banner = false;
    const auto result = mpf::Transpiler{}.transpile(discarded, options);
    const auto capture = mpf::Transpiler{}.transpile(implicit, options);
    REQUIRE(result.success());
    REQUIRE(capture.success());
    const auto projection =
        target == mpf::TargetLanguage::cpp ? "std::get<0>(checked())" : "(checked())[0]";
    REQUIRE(result.code.find(projection) == std::string::npos);
    REQUIRE(capture.code.find(projection) != std::string::npos);
    REQUIRE(std::any_of(result.source_map.segments.begin(), result.source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 1U; }));
    REQUIRE(result.code == mpf::Transpiler{}.transpile(discarded, options).code);
  }
}
