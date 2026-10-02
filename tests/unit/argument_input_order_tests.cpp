#include <algorithm>
#include <string>

#include "backends/common/identifier_mangler.hpp"
#include "backends/common/lir_builder.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "ir/dump.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

const std::string source =
    "disp(checked(seed(1),2))\n"
    "function output = checked(T0,second)\narguments\n"
    "T0 (1,1) double {mustBePositive}\nsecond (1,1) logical = 1\nend\n"
    "output = T0 + second\nend\n"
    "function output = seed(value)\narguments\nvalue (1,1) double\nend\n"
    "output = value\nend\n";

mir::Program lower(const std::string& text) {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(text, "input_order.m"));
  REQUIRE(parsed.diagnostics.empty());
  auto lowered = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.diagnostics.empty());
  auto result = mir::lower_from_hir(std::move(lowered.program), std::move(analysis.semantics),
                                    analysis.names);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(mir::verify(result.program, "input-order").empty());
  return std::move(result.program);
}

cpp::lir::SemanticProgram planned() {
  const auto program = lower(source);
  auto result = lower_structured_lir<cpp::lir::SemanticProgram, cpp::lir::Statement,
                                     cpp::lir::Expression, cpp::lir::CaseSelector>(
      program, [](HirNodeId, IntrinsicId) { return CodeBinding{}; });
  result->source_language = mpf::SourceLanguage::matlab;
  result->runtime.require(cpp::lir::RuntimeFeature::argument_validation);
  result->identifiers =
      allocate_identifiers(mpf::TargetLanguage::cpp, collect_identifier_inventory(*result));
  cpp::plan_lir_resources(*result, mpf::TranspileOptions{});
  cpp::plan_lir_representation(*result);
  std::vector<mpf::Diagnostic> diagnostics;
  cpp::verify_lir_resources(*result, diagnostics);
  cpp::verify_lir_representation(*result, diagnostics);
  if (!diagnostics.empty()) throw std::runtime_error(diagnostics.front().message);
  return std::move(*result);
}

cpp::lir::Statement& checked(cpp::lir::SemanticProgram& program) {
  const auto result =
      std::find_if(program.statements.begin(), program.statements.end(),
                   [](const auto& statement) { return statement.name == "checked"; });
  REQUIRE(result != program.statements.end());
  return *result;
}
}  // namespace

TEST_CASE("MIR fixes Matlab conversion execution to callee entry independently of target") {
  const auto pristine = lower(source);
  REQUIRE(dump_mir(pristine).find("boundary-execution=1") != std::string::npos);
  for (const auto execution :
       {ArgumentBoundaryExecution::none, static_cast<ArgumentBoundaryExecution>(255U)}) {
    auto invalid = pristine;
    auto call = std::find_if(invalid.calls.begin(), invalid.calls.end(), [](const auto& entry) {
      return !entry.arguments.empty() && entry.arguments.front().boundary.execution ==
                                             ArgumentBoundaryExecution::matlab_callee_entry;
    });
    REQUIRE(call != invalid.calls.end());
    call->arguments.front().boundary.execution = execution;
    REQUIRE(!mir::verify(invalid, "corrupt-input-execution").empty());
  }
}

TEST_CASE("cpp raw input ABI is private collision safe and normalizes optional values locally") {
  auto program = planned();
  const auto& function = checked(program);
  REQUIRE(function.function_abi.parameters.size() == 2U);
  REQUIRE(function.function_abi.parameters[0].passing ==
          cpp::lir::ParameterPassing::matlab_raw_input);
  REQUIRE(function.function_abi.parameters[1].passing ==
          cpp::lir::ParameterPassing::matlab_raw_optional_input);
  for (std::size_t ordinal = 0U; ordinal < 2U; ++ordinal) {
    const auto& abi = function.function_abi.parameters[ordinal];
    REQUIRE(!abi.template_parameter.empty());
    REQUIRE(abi.template_parameter != "T0");
    REQUIRE(abi.raw_name != function.parameters[ordinal]);
    REQUIRE(abi.raw_name == *program.temporaries.find(
                                function.id, cpp::lir::TemporaryRole::matlab_raw_input, ordinal));
    REQUIRE(function.plan.argument_inputs[ordinal].raw_name == abi.raw_name);
  }
  REQUIRE(function.plan.argument_inputs[0].form == cpp::lir::ArgumentInputForm::matlab_double);
  REQUIRE(function.plan.argument_inputs[1].form == cpp::lir::ArgumentInputForm::matlab_logical);
  REQUIRE(function.body.front().expression.children[1].plan.variable_access ==
          cpp::lir::VariableAccess::optional_value);
}

TEST_CASE("cpp input and ABI verifiers reject corrupted materialization plans and identities") {
  const auto pristine = planned();
  for (const auto mutation : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}) {
    auto invalid = pristine;
    auto& function = checked(invalid);
    auto& input = function.plan.argument_inputs[0];
    auto& abi = function.function_abi.parameters[0];
    if (mutation == 0) input.form = cpp::lir::ArgumentInputForm::direct;
    if (mutation == 1) input.rank = 2U;
    if (mutation == 2) input.dimensions[0].extent = 7U;
    if (mutation == 3) input.raw_name = "T0";
    if (mutation == 4) input.template_type = "T0";
    if (mutation == 5) input.concrete_type = "bool";
    if (mutation == 6) abi.passing = cpp::lir::ParameterPassing::value;
    if (mutation == 7) abi.raw_name = "T0";
    if (mutation == 8) abi.template_parameter = "T0";
    if (mutation == 9) function.plan.argument_inputs.clear();
    if (mutation == 10) {
      auto slot = std::find_if(invalid.temporaries.slots.begin(), invalid.temporaries.slots.end(),
                               [&](const auto& entry) { return entry.name == abi.raw_name; });
      REQUIRE(slot != invalid.temporaries.slots.end());
      slot->role = cpp::lir::TemporaryRole::comparison_operand;
    }
    std::vector<mpf::Diagnostic> diagnostics;
    cpp::verify_lir_resources(invalid, diagnostics);
    cpp::verify_lir_representation(invalid, diagnostics);
    REQUIRE(!diagnostics.empty());
  }
}

TEST_CASE("cpp call plans sequence nontrivial Matlab actuals before entry normalization") {
  auto program = planned();
  auto& call = program.statements.front().expression;
  REQUIRE(call.kind == ExpressionKind::call);
  REQUIRE(call.plan.evaluation == cpp::lir::EvaluationForm::ordered_call_reference_lambda_iife);
  REQUIRE(call.plan.call_arguments[0].boundary_form ==
          cpp::lir::CallBoundaryForm::matlab_callee_entry);
  REQUIRE(program.temporaries.find(call.id, cpp::lir::TemporaryRole::call_argument, 0U) != nullptr);
  call.plan.evaluation = cpp::lir::EvaluationForm::direct;
  std::vector<mpf::Diagnostic> diagnostics;
  cpp::verify_lir_representation(program, diagnostics);
  REQUIRE(!diagnostics.empty());
  program = planned();
  program.statements.front().expression.plan.call_arguments[0].boundary_form =
      cpp::lir::CallBoundaryForm::native;
  diagnostics.clear();
  cpp::verify_lir_representation(program, diagnostics);
  REQUIRE(!diagnostics.empty());
}

TEST_CASE("cpp trivial Matlab actuals and Python parameter ABI retain direct native forms") {
  mpf::TranspileOptions options;
  options.target = mpf::TargetLanguage::cpp;
  options.language = mpf::SourceLanguage::matlab;
  const auto matlab = mpf::Transpiler{}.transpile(
      "disp(add(20,22))\nfunction output = add(left,right)\noutput = left + right\nend\n", options);
  REQUIRE(matlab.success());
  REQUIRE(matlab.code.find("add(20, 22)") != std::string::npos);
  REQUIRE(matlab.code.find("mpf_internal_raw_input_") == std::string::npos);
  options.language = mpf::SourceLanguage::python;
  const auto python =
      mpf::Transpiler{}.transpile("def add(value):\n  return value + 1\nprint(add(41))\n", options);
  REQUIRE(python.success());
  REQUIRE(python.code.find("mpf_internal_raw_input_") == std::string::npos);
  REQUIRE(python.code.find("template <typename T0>") != std::string::npos);
}

TEST_CASE("cpp ordered call type probes remain legal unevaluated C++17 expressions") {
  mpf::TranspileOptions options;
  options.target = mpf::TargetLanguage::cpp;
  options.language = mpf::SourceLanguage::matlab;
  const auto result = mpf::Transpiler{}.transpile(
      "values = copy([1,2],3)\ndisp(length(values))\n"
      "function output = copy(value,marker)\noutput = value\nend\n",
      options);
  REQUIRE(result.success());
  REQUIRE(result.code.find("std::decay_t<decltype(copy(") != std::string::npos);
  REQUIRE(result.code.find("decltype(([&]()") == std::string::npos);
  REQUIRE(result.code.find("return copy(mpf_internal_call_argument_") != std::string::npos);
}
