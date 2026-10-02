#include <algorithm>
#include <string>
#include <utility>

#include "backends/common/lir_builder.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "ir/dump.hpp"
#include "mpf/transpiler.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

const std::string source =
    "function output = checked()\narguments (Output)\noutput (1,1) logical\nend\n"
    "output = 2;\ndisp(output);\noutput = output + 1;\ndisp(output);\nend\n";

struct Analyzed {
  hir::Program program;
  AnalysisResult analysis;
};

Analyzed analyze(const std::string& text = source) {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(text, "output_contract.m"));
  REQUIRE(parsed.diagnostics.empty());
  auto lowered = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.diagnostics.empty());
  REQUIRE(hir::verify_semantics(lowered.program, analysis.semantics, "output-contract").empty());
  return {std::move(lowered.program), std::move(analysis)};
}

mir::Program lower(const std::string& text = source) {
  auto analyzed = analyze(text);
  auto result = mir::lower_from_hir(
      std::move(analyzed.program), std::move(analyzed.analysis.semantics), analyzed.analysis.names);
  REQUIRE(result.diagnostics.empty());
  return std::move(result.program);
}

mpf::TranspileResult compile(const std::string& text, const mpf::TargetLanguage target) {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::matlab;
  options.target = target;
  options.emit_source_banner = false;
  return mpf::Transpiler{}.transpile(text, options);
}

template <typename Program, typename Statement, typename Expression, typename Selector>
Program project(const mir::Program& program) {
  auto result = lower_structured_lir<Program, Statement, Expression, Selector>(
      program, [](HirNodeId, IntrinsicId) { return CodeBinding{}; });
  result->source_language = mpf::SourceLanguage::matlab;
  return std::move(*result);
}
}  // namespace

TEST_CASE(
    "Matlab output declarations constrain caller results without pretyping workspace locals") {
  const auto analyzed = analyze();
  const auto& function = analyzed.program.statements.front();
  const auto& facts = *analyzed.analysis.semantics.statement(function.id);
  REQUIRE(facts.return_types == std::vector<ValueType>{ValueType::boolean});
  REQUIRE(facts.return_numeric_types == std::vector<NumericType>{logical_numeric_type});
  REQUIRE(facts.declared_type == ValueType::boolean);
  REQUIRE(analyzed.analysis.semantics.statement(function.body[0].id)->declared_type ==
          ValueType::integer);
  REQUIRE(analyzed.analysis.semantics.statement(function.body[2].id)->declared_type ==
          ValueType::integer);
  const auto program = lower();
  const auto& result = program.functions.back();
  REQUIRE(mir::value_type(program, result.result_types.front()) == ValueType::boolean);
}

TEST_CASE("Matlab normalized output metadata infers wildcard extents after column-major reshape") {
  const auto analyzed = analyze(
      "function output = checked()\narguments (Output)\n"
      "output (1,:) double\nend\noutput = [1 3; 2 4];\nend\n");
  const auto& facts =
      *analyzed.analysis.semantics.statement(analyzed.program.statements.front().id);
  REQUIRE(facts.return_types.front() == ValueType::list);
  REQUIRE(facts.return_shapes.front() == (std::vector<std::size_t>{1U, 4U}));
  REQUIRE(facts.return_element_types.front() == ValueType::real);
  REQUIRE(facts.return_element_numeric_types.front() == real_numeric_type);
  REQUIRE(facts.argument_validations.front().validated_rank == 2U);
  const std::string unknown_source =
      "function output = checked(value)\narguments (Output)\n"
      "output (1,:) double\nend\noutput = value;\nend\n";
  const auto unknown = analyze(unknown_source);
  const auto& unknown_facts =
      *unknown.analysis.semantics.statement(unknown.program.statements.front().id);
  REQUIRE(unknown_facts.return_shapes.front() == (std::vector<std::size_t>{1U, dynamic_extent}));
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp})
    REQUIRE(compile("values = checked([1; 2; 3]);\ndisp(values(1,3));\n" + unknown_source, target)
                .success());
}

TEST_CASE(
    "Matlab output expansion metadata uses declared rank rather than raw scalar workspace rank") {
  const auto analyzed = analyze(
      "function output = checked()\narguments (Output)\n"
      "output (2,3,4) logical\nend\noutput = 2;\nend\n");
  const auto& facts =
      *analyzed.analysis.semantics.statement(analyzed.program.statements.front().id);
  REQUIRE(facts.return_shapes.front() == (std::vector<std::size_t>{2U, 3U, 4U}));
  REQUIRE(facts.return_types.front() == ValueType::list);
  REQUIRE(facts.return_element_types.front() == ValueType::boolean);
  REQUIRE(facts.argument_validations.front().validated_rank == 3U);
}

TEST_CASE("Matlab logical output call sites receive logical display plans in both targets") {
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto result = compile("disp(checked());\n" + source, target);
    REQUIRE(result.success());
    REQUIRE(result.code.find("output = __mpf_validate_argument(output") == std::string::npos);
    if (target == mpf::TargetLanguage::javascript)
      REQUIRE(result.code.find("console.log(Number(checked()))") != std::string::npos);
    else
      REQUIRE(result.code.find("convert_argument_logical<0>(output, ") != std::string::npos);
  }
}

TEST_CASE(
    "Matlab output contracts independently reject raw result types and corrupt declared extents") {
  for (const auto mutation : {0, 1, 2, 3, 4}) {
    auto analyzed = analyze(
        "function output = checked()\narguments (Output)\n"
        "output (2,3) logical\nend\noutput = 2;\nend\n");
    auto& facts = *analyzed.analysis.semantics.statement(analyzed.program.statements.front().id);
    if (mutation == 0) facts.return_types.front() = ValueType::integer;
    if (mutation == 1) facts.return_element_types.front() = ValueType::integer;
    if (mutation == 2) facts.return_shapes.front()[0] = 1U;
    if (mutation == 3) facts.argument_validations.front().validated_rank = 0U;
    if (mutation == 4) facts.return_array_storage.front() = ArrayStorageFormat::sparse_csc;
    REQUIRE(!hir::verify_semantics(analyzed.program, analyzed.analysis.semantics,
                                   "corrupt-output-contract")
                 .empty());
  }
}

TEST_CASE(
    "JavaScript output plans reject corrupt private class rank dimensions and temporary "
    "identities") {
  const auto program = lower();
  for (const auto mutation : {0, 1, 2, 3, 4}) {
    auto projected = project<javascript::lir::SemanticProgram, javascript::lir::Statement,
                             javascript::lir::Expression, javascript::lir::CaseSelector>(program);
    projected.runtime.require(javascript::lir::RuntimeFeature::argument_validation);
    projected.runtime.require(javascript::lir::RuntimeFeature::arrays);
    projected.runtime.require(javascript::lir::RuntimeFeature::complex_numbers);
    javascript::plan_lir_resources(projected, mpf::TranspileOptions{});
    javascript::plan_lir_representation(projected);
    std::vector<mpf::Diagnostic> pristine;
    javascript::verify_lir_representation(projected, pristine);
    javascript::verify_lir_resources(projected, pristine);
    REQUIRE(pristine.empty());
    auto& output = projected.statements.front().plan.argument_outputs.front();
    if (mutation == 0) output.class_opcode = 1U;
    if (mutation == 1) output.rank = 2U;
    if (mutation == 2) output.dimensions.clear();
    if (mutation == 3) output.ordinal = 99U;
    if (mutation == 4) projected.temporaries.slots.front().name.clear();
    std::vector<mpf::Diagnostic> diagnostics;
    javascript::verify_lir_representation(projected, diagnostics);
    javascript::verify_lir_resources(projected, diagnostics);
    REQUIRE(!diagnostics.empty());
  }
}

TEST_CASE(
    "cpp output plans reject corrupt materialization rank dimensions and temporary identities") {
  const auto program = lower();
  for (const auto mutation : {0, 1, 2, 3, 4}) {
    auto projected = project<cpp::lir::SemanticProgram, cpp::lir::Statement, cpp::lir::Expression,
                             cpp::lir::CaseSelector>(program);
    projected.runtime.require(cpp::lir::RuntimeFeature::argument_validation);
    cpp::plan_lir_resources(projected, mpf::TranspileOptions{});
    cpp::plan_lir_representation(projected);
    std::vector<mpf::Diagnostic> pristine;
    cpp::verify_lir_representation(projected, pristine);
    cpp::verify_lir_resources(projected, pristine);
    REQUIRE(pristine.empty());
    auto& output = projected.statements.front().plan.argument_outputs.front();
    if (mutation == 0) output.form = cpp::lir::ArgumentOutputForm::matlab_double;
    if (mutation == 1) output.rank = 2U;
    if (mutation == 2) output.dimensions.clear();
    if (mutation == 3) output.ordinal = 99U;
    if (mutation == 4) projected.temporaries.slots.front().name.clear();
    std::vector<mpf::Diagnostic> diagnostics;
    cpp::verify_lir_representation(projected, diagnostics);
    cpp::verify_lir_resources(projected, diagnostics);
    REQUIRE(!diagnostics.empty());
  }
}

TEST_CASE(
    "Matlab double output contracts preserve complex storage while logical outputs normalize it") {
  for (const auto* class_name : {"double", "logical"}) {
    const std::string text = "function output = checked()\narguments (Output)\noutput (1,1) " +
                             std::string(class_name) + "\nend\noutput = 2i;\nend\n";
    const auto analyzed = analyze(text);
    const auto& facts =
        *analyzed.analysis.semantics.statement(analyzed.program.statements.front().id);
    const bool logical = std::string(class_name) == "logical";
    REQUIRE(facts.return_numeric_types.front() ==
            (logical ? logical_numeric_type : complex_numeric_type));
    for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp})
      REQUIRE(compile(text, target).success());
  }
}

TEST_CASE("Matlab shared input and output names retain the initialized input workspace binding") {
  const std::string text =
      "function value = checked(value)\narguments\nvalue (1,1) double\nend\n"
      "arguments (Output)\nvalue (1,1) logical\nend\nend\n";
  const auto analyzed = analyze(text);
  const auto& facts =
      *analyzed.analysis.semantics.statement(analyzed.program.statements.front().id);
  REQUIRE(facts.parameter_types.front() == ValueType::real);
  REQUIRE(facts.return_types.front() == ValueType::boolean);
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp})
    REQUIRE(compile("disp(checked(2));\n" + text, target).success());
}

TEST_CASE(
    "Matlab size-free validators retain array output metadata without inventing a conversion") {
  const std::string text =
      "function output = checked()\narguments (Output)\noutput {mustBeNumeric}\nend\n"
      "output = [1 2];\nend\n";
  const auto analyzed = analyze(text);
  const auto& facts =
      *analyzed.analysis.semantics.statement(analyzed.program.statements.front().id);
  REQUIRE(facts.return_types.front() == ValueType::list);
  REQUIRE(facts.return_shapes.front() == std::vector<std::size_t>{2U});
  REQUIRE(facts.return_element_numeric_types.front() == real_numeric_type);
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp})
    REQUIRE(compile("values = checked();\ndisp(values(2));\n" + text, target).success());
}
