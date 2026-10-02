#include <algorithm>
#include <utility>

#include "backends/common/lir_builder.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "ir/mir.hpp"
#include "semantic/analyzer.hpp"
#include "source/source_manager.hpp"
#include "test_framework.hpp"

namespace {

mpf::detail::mir::LoweringResult lower_validated_function() {
  mpf::detail::SourceManager sources;
  const auto source_id = sources.add(
      "function output = bounded(limits, lower, value)\n"
      "arguments\n"
      "limits (1,:) double\n"
      "lower (1,1) double = 1\n"
      "value (1,1) double {mustBeGreaterThan(value,lower)} = lower + 1\n"
      "end\n"
      "output = value\n"
      "end\n",
      "validator_formals.m");
  const auto& frontend = mpf::detail::matlab_frontend();
  auto parsed = mpf::detail::parse_with_frontend(frontend, sources.source(source_id));
  REQUIRE(parsed.diagnostics.empty());
  auto hir = frontend.lower(std::move(parsed.ast));
  REQUIRE(hir.diagnostics.empty());
  auto analysis = mpf::detail::analyze_program(hir.program, std::move(hir.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(hir.program), std::move(analysis.semantics),
                                              analysis.names);
  REQUIRE(mir.diagnostics.empty());
  return mir;
}

template <typename Program, typename Mutate, typename Verify>
void require_rejected(const Program& pristine, Mutate mutate, Verify verify) {
  auto invalid = pristine;
  mutate(invalid);
  std::vector<mpf::Diagnostic> diagnostics;
  verify(invalid, diagnostics);
  REQUIRE(!diagnostics.empty());
}

}  // namespace

TEST_CASE("MIR independently rejects a validator threshold rebound to an array formal") {
  auto mir = lower_validated_function();
  REQUIRE(mpf::detail::mir::verify(mir.program, "validator-formals").empty());
  auto reordered = mir.program;
  auto reordered_function = std::find_if(
      reordered.statements.begin() + 1, reordered.statements.end(),
      [](const auto& statement) { return statement.kind == mpf::detail::StatementKind::function; });
  REQUIRE(reordered_function != reordered.statements.end());
  std::swap(reordered_function->argument_validations[1],
            reordered_function->argument_validations[2]);
  REQUIRE(!mpf::detail::mir::verify(reordered, "reordered-validator-defaults").empty());
  auto function = std::find_if(
      mir.program.statements.begin() + 1, mir.program.statements.end(),
      [](const auto& statement) { return statement.kind == mpf::detail::StatementKind::function; });
  REQUIRE(function != mir.program.statements.end());
  auto& operand = function->argument_validations[2].validators.front().operands.front();
  REQUIRE(operand.input_ordinal == 1U);
  operand.input_ordinal = 0U;
  const auto diagnostics = mpf::detail::mir::verify(mir.program, "array-validator-threshold");
  REQUIRE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
    return diagnostic.message.find("scalar numeric formal ABI") != std::string::npos;
  }));
}

TEST_CASE(
    "JavaScript validator call plans reject corrupt opcodes symbols tokens and formal types") {
  namespace js = mpf::detail::javascript;
  const auto mir = lower_validated_function();
  auto program = mpf::detail::lower_structured_lir<js::lir::SemanticProgram, js::lir::Statement,
                                                   js::lir::Expression, js::lir::CaseSelector>(
      mir.program, [](const mpf::detail::HirNodeId, const mpf::detail::IntrinsicId) {
        return mpf::detail::CodeBinding{};
      });
  program->source_language = mpf::SourceLanguage::matlab;
  program->runtime.require(js::lir::RuntimeFeature::argument_validation);
  program->runtime.require(js::lir::RuntimeFeature::arrays);
  program->runtime.require(js::lir::RuntimeFeature::complex_numbers);
  js::plan_lir_resources(*program, mpf::TranspileOptions{});
  js::plan_lir_representation(*program);
  std::vector<mpf::Diagnostic> diagnostics;
  js::verify_lir_representation(*program, diagnostics);
  REQUIRE(diagnostics.empty());
  const auto& call = program->statements.front().plan.argument_validators[2].front();
  REQUIRE(call.opcode == 23U);
  REQUIRE(call.operands.front().form == js::lir::ValidatorOperandForm::parameter_value);
  REQUIRE(call.operands.front().symbol.valid());

  require_rejected(
      *program,
      [](auto& invalid) {
        auto& plans = invalid.statements.front().argument_validations;
        std::swap(plans[1], plans[2]);
        js::plan_lir_representation(invalid);
      },
      js::verify_lir_representation);

  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].front().opcode = 255U;
      },
      js::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].front().operands.front().symbol = {};
      },
      js::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].front().operands.front().token =
            "injected";
      },
      js::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].front().operands.front().form =
            js::lir::ValidatorOperandForm::numeric_literal;
      },
      js::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front()
            .argument_validations[2]
            .validators.front()
            .operands.front()
            .input_ordinal = 0U;
        js::plan_lir_representation(invalid);
      },
      js::verify_lir_representation);
}

TEST_CASE("cpp validator call plans validate optional access and reject array thresholds") {
  namespace cpp = mpf::detail::cpp;
  const auto mir = lower_validated_function();
  auto program = mpf::detail::lower_structured_lir<cpp::lir::SemanticProgram, cpp::lir::Statement,
                                                   cpp::lir::Expression, cpp::lir::CaseSelector>(
      mir.program, [](const mpf::detail::HirNodeId, const mpf::detail::IntrinsicId) {
        return mpf::detail::CodeBinding{};
      });
  program->source_language = mpf::SourceLanguage::matlab;
  program->runtime.require(cpp::lir::RuntimeFeature::argument_validation);
  cpp::plan_lir_resources(*program, mpf::TranspileOptions{});
  cpp::plan_lir_representation(*program);
  std::vector<mpf::Diagnostic> diagnostics;
  cpp::verify_lir_representation(*program, diagnostics);
  REQUIRE(diagnostics.empty());
  const auto& call = program->statements.front().plan.argument_validators[2].front();
  REQUIRE(call.opcode == 23U);
  REQUIRE(call.operands.front().form == cpp::lir::ValidatorOperandForm::optional_parameter_value);
  REQUIRE(call.operands.front().symbol.valid());

  require_rejected(
      *program,
      [](auto& invalid) {
        auto& plans = invalid.statements.front().argument_validations;
        std::swap(plans[1], plans[2]);
        cpp::plan_lir_representation(invalid);
      },
      cpp::verify_lir_representation);

  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].front().operands.front().form =
            cpp::lir::ValidatorOperandForm::parameter_value;
      },
      cpp::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].front().operands.front().symbol = {};
      },
      cpp::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].front().operands.front().token =
            "injected";
      },
      cpp::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].front().opcode = 255U;
      },
      cpp::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front()
            .argument_validations[2]
            .validators.front()
            .operands.front()
            .input_ordinal = 0U;
        cpp::plan_lir_representation(invalid);
      },
      cpp::verify_lir_representation);
}
