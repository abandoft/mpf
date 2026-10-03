#include <algorithm>
#include <string>
#include <vector>

#include "backends/common/lir_builder.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "mpf/transpiler.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

mir::Program lower_exception_source() {
  auto parsed = parse_with_frontend(
      matlab_frontend(),
      SourceText("function result = checked(value)\narguments\n"
                 "value (1,1) double {mustBePositive}\nend\n"
                 "arguments (Output)\nresult (1,1) double {mustBePositive}\nend\n"
                 "result = value;\nend\n",
                 "validator_exception.m"));
  REQUIRE(parsed.diagnostics.empty());
  auto hir = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(hir.diagnostics.empty());
  auto analysis = analyze_program(hir.program, std::move(hir.semantics));
  REQUIRE(analysis.empty());
  auto result =
      mir::lower_from_hir(std::move(hir.program), std::move(analysis.semantics), analysis.names);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(mir::verify(result.program, "validator-exception").empty());
  return std::move(result.program);
}

template <typename Program, typename Statement, typename Expression, typename Selector>
Program project_exception_source() {
  const auto source = lower_exception_source();
  auto projected = lower_structured_lir<Program, Statement, Expression, Selector>(
      source, [](HirNodeId, IntrinsicId) { return CodeBinding{}; });
  projected->source_language = mpf::SourceLanguage::matlab;
  return std::move(*projected);
}
}  // namespace

TEST_CASE("JavaScript argument runtime closes its native exception dependency explicitly") {
  namespace js = javascript::lir;
  js::RuntimeRequirements runtime;
  REQUIRE(runtime.bits == 0U);
  runtime.require(js::RuntimeFeature::argument_validation);
  REQUIRE(runtime.contains(js::RuntimeFeature::exception_handling));
  const auto original = runtime.bits;
  runtime.require(js::RuntimeFeature::argument_validation);
  REQUIRE(runtime.bits == original);
  js::RuntimeRequirements plain;
  plain.require(js::RuntimeFeature::arrays);
  REQUIRE(!plain.contains(js::RuntimeFeature::exception_handling));
}

TEST_CASE("cpp argument runtime closes its native exception dependency explicitly") {
  namespace target = cpp::lir;
  target::RuntimeRequirements runtime;
  REQUIRE(runtime.bits == 0U);
  runtime.require(target::RuntimeFeature::argument_validation);
  REQUIRE(runtime.contains(target::RuntimeFeature::exception_handling));
  const auto original = runtime.bits;
  runtime.require(target::RuntimeFeature::argument_validation);
  REQUIRE(runtime.bits == original);
  target::RuntimeRequirements plain;
  plain.require(target::RuntimeFeature::arrays);
  REQUIRE(!plain.contains(target::RuntimeFeature::exception_handling));
}

TEST_CASE(
    "JavaScript independently rejects lost validator exceptions even after resource replanning") {
  namespace js = javascript::lir;
  auto program = project_exception_source<js::SemanticProgram, js::Statement, js::Expression,
                                          js::CaseSelector>();
  program.runtime.require(js::RuntimeFeature::argument_validation);
  program.runtime.require(js::RuntimeFeature::arrays);
  program.runtime.require(js::RuntimeFeature::complex_numbers);
  javascript::plan_lir_resources(program, mpf::TranspileOptions{});
  javascript::plan_lir_representation(program);
  std::vector<mpf::Diagnostic> diagnostics;
  javascript::verify_lir_representation(program, diagnostics);
  javascript::verify_lir_resources(program, diagnostics);
  REQUIRE(diagnostics.empty());
  const auto& fragments = program.module.runtime_fragments;
  const auto exception =
      std::find(fragments.begin(), fragments.end(), js::RuntimeFragment::exception_handling);
  const auto validation =
      std::find(fragments.begin(), fragments.end(), js::RuntimeFragment::argument_validation);
  REQUIRE(exception != fragments.end());
  REQUIRE(validation != fragments.end());
  REQUIRE(exception < validation);
  program.runtime.bits &=
      ~(1U << static_cast<std::uint32_t>(js::RuntimeFeature::exception_handling));
  javascript::plan_lir_resources(program, mpf::TranspileOptions{});
  diagnostics.clear();
  javascript::verify_lir_representation(program, diagnostics);
  REQUIRE(!diagnostics.empty());
}

TEST_CASE("cpp independently rejects lost validator exceptions even after resource replanning") {
  namespace target = cpp::lir;
  auto program = project_exception_source<target::SemanticProgram, target::Statement,
                                          target::Expression, target::CaseSelector>();
  program.runtime.require(target::RuntimeFeature::argument_validation);
  cpp::plan_lir_resources(program, mpf::TranspileOptions{});
  cpp::plan_lir_representation(program);
  std::vector<mpf::Diagnostic> diagnostics;
  cpp::verify_lir_representation(program, diagnostics);
  cpp::verify_lir_resources(program, diagnostics);
  REQUIRE(diagnostics.empty());
  const auto& fragments = program.translation_unit.runtime_fragments;
  const auto exception =
      std::find(fragments.begin(), fragments.end(), target::RuntimeFragment::exception_handling);
  const auto validation =
      std::find(fragments.begin(), fragments.end(), target::RuntimeFragment::argument_validation);
  REQUIRE(exception != fragments.end());
  REQUIRE(validation != fragments.end());
  REQUIRE(exception < validation);
  program.runtime.bits &=
      ~(1U << static_cast<std::uint32_t>(target::RuntimeFeature::exception_handling));
  cpp::plan_lir_resources(program, mpf::TranspileOptions{});
  diagnostics.clear();
  cpp::verify_lir_representation(program, diagnostics);
  REQUIRE(!diagnostics.empty());
}
