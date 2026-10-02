#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "backends/common/lir_builder.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "ir/dump.hpp"
#include "ir/semantic_table.hpp"
#include "mpf/transpiler.hpp"
#include "semantic/analyzer.hpp"
#include "source/source_manager.hpp"
#include "test_framework.hpp"

namespace {

constexpr auto source =
    "function output = bounded(lower, upper, value)\n"
    "arguments\n"
    "lower (1,1) double = 0\n"
    "upper (1,1) double = 10\n"
    "value (1,1) double {mustBeFinite(value), "
    "mustBeInRange(value,lower,upper,'exclude-lower',\"exclude-upper\")} = 1\n"
    "end\n"
    "arguments (Output)\n"
    "output (1,1) double {mustBeInRange(output,lower,upper)}\n"
    "end\n"
    "output = value\n"
    "end\n";

mpf::TranspileResult compile(const std::string& text, const mpf::TargetLanguage target,
                             const mpf::LanguageVersion version = {}) {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::matlab;
  options.target = target;
  options.language_version = version;
  options.emit_source_banner = false;
  return mpf::Transpiler{}.transpile(text, options);
}

bool diagnostic(const mpf::TranspileResult& result, const std::string& code) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [&](const auto& entry) { return entry.code == code; });
}

void inject_range_boundary_byte(mpf::detail::ArgumentRangeBoundary& boundary,
                                const std::uint8_t encoded) noexcept {
  // Negative verifier fixtures deliberately inject malformed serialized enum bytes. Do not
  // construct a normal semantic enum through an out-of-catalog cast or disable analysis rules.
  static_assert(sizeof(boundary) == sizeof(encoded));
  std::memcpy(&boundary, &encoded, sizeof(boundary));
}

mpf::detail::mir::LoweringResult lower_range() {
  mpf::detail::SourceManager sources;
  const auto id = sources.add(source, "argument_range.m");
  const auto& frontend = mpf::detail::matlab_frontend();
  auto parsed = mpf::detail::parse_with_frontend(frontend, sources.source(id));
  REQUIRE(parsed.diagnostics.empty());
  const auto* ast = std::get_if<mpf::detail::matlab::ast::Program>(&parsed.ast);
  REQUIRE(ast != nullptr);
  const auto& root = ast->records[ast->roots.front().value()];
  const auto& syntax = ast->statements[root.index].argument_declarations[2].validators.back();
  REQUIRE(syntax.name == "mustBeInRange");
  REQUIRE(syntax.argument_count == 5U);
  REQUIRE(syntax.explicit_call);
  auto hir = frontend.lower(std::move(parsed.ast));
  REQUIRE(hir.diagnostics.empty());
  auto analysis = mpf::detail::analyze_program(hir.program, std::move(hir.semantics));
  REQUIRE(analysis.empty());
  REQUIRE(mpf::detail::hir::verify_semantics(hir.program, analysis.semantics, "range").empty());
  auto* facts = analysis.semantics.statement(hir.program.statements.front().id);
  REQUIRE(facts != nullptr);
  const auto boundary = facts->argument_validations[2].validators.back().range_boundary;
  REQUIRE(boundary == mpf::detail::ArgumentRangeBoundary::exclusive);
  facts->argument_validations[2].validators.back().range_boundary =
      mpf::detail::ArgumentRangeBoundary::inclusive;
  REQUIRE(
      !mpf::detail::hir::verify_semantics(hir.program, analysis.semantics, "bad-range").empty());
  facts->argument_validations[2].validators.back().range_boundary = boundary;
  auto result = mpf::detail::mir::lower_from_hir(std::move(hir.program),
                                                 std::move(analysis.semantics), analysis.names);
  REQUIRE(result.diagnostics.empty());
  return result;
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

TEST_CASE("Matlab validators accept explicit unary calls and enforce complete call arity") {
  const std::string prefix = "function output = checked(value)\narguments\nvalue (1,1) double {";
  const std::string suffix = "}\nend\noutput = value\nend\n";
  const auto validator_source = [&](const std::string_view call) {
    std::string text;
    text.reserve(prefix.size() + call.size() + suffix.size());
    text.append(prefix);
    text.append(call);
    text.append(suffix);
    return text;
  };
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    REQUIRE(
        compile(validator_source("mustBeFinite(value),mustBeReal(value),mustBePositive"), target)
            .success());
    for (const auto* name : {"mustBeNumeric",
                             "mustBeNumericOrLogical",
                             "mustBeFloat",
                             "mustBeReal",
                             "mustBeFinite",
                             "mustBeNonNan",
                             "mustBePositive",
                             "mustBeNonpositive",
                             "mustBeNonnegative",
                             "mustBeNegative",
                             "mustBeNonzero",
                             "mustBeInteger",
                             "mustBeNonempty",
                             "mustBeScalarOrEmpty",
                             "mustBeVector",
                             "mustBeRow",
                             "mustBeColumn",
                             "mustBeMatrix",
                             "mustBeNonmissing",
                             "mustBeNonzeroLengthText",
                             "mustBeText",
                             "mustBeTextScalar",
                             "mustBeValidVariableName"}) {
      std::string call{name};
      call.append("(value)");
      REQUIRE(compile(validator_source(call), target).success());
    }
    for (const auto* call :
         {"mustBeReal()", "mustBeReal(other)", "mustBeReal(value,1)", "mustBeInRange(value,0)",
          "mustBeInRange(value,0,1,)", "mustBeInRange(value,0,1,'closed')",
          "mustBeInRange(value,0,1,'exclusive','exclusive','exclusive')",
          "mustBeInRange(value,0,1,flag)", "mustBeInRange(value,0,1 + 2)",
          "mustBeInRange(value,[0],1)", "mustBeInRange(value,0,1i)", "mustBeInRange"}) {
      const auto failed = compile(validator_source(call), target);
      REQUIRE(!failed.success());
      REQUIRE(failed.code.empty());
      REQUIRE(diagnostic(failed, "MPF2060") || diagnostic(failed, "MPF1200"));
      REQUIRE(!diagnostic(failed, "MPF0005"));
      REQUIRE(!diagnostic(failed, "MPF0006"));
    }
    const auto range = validator_source("mustBeInRange(value,0,1)");
    const auto unsupported_hex = compile(validator_source("mustBeInRange(value,0x10,20)"), target);
    REQUIRE(!unsupported_hex.success());
    REQUIRE(diagnostic(unsupported_hex, "MPF1012"));
    REQUIRE(!diagnostic(unsupported_hex, "MPF0005"));
    REQUIRE(compile(range, target, {2020, 2}).success());
    const auto old = compile(range, target, {2020, 1});
    REQUIRE(!old.success());
    REQUIRE(diagnostic(old, "MPF1201"));
  }
}

TEST_CASE("Matlab scalar length and numel do not weaken Python sized-object checks") {
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    REQUIRE(compile("disp(length(1),numel(1),length(1i),numel(1i))\n", target).success());
    const auto vector_sum = compile(
        "function output = total(value)\narguments\nvalue (1,:) double\nend\n"
        "output = sum(value)\nend\n",
        target);
    REQUIRE(vector_sum.success());
    const auto matrix_sum = compile(
        "function output = total(value)\narguments\nvalue (:,:) double\nend\n"
        "output = sum(value)\nend\n",
        target);
    REQUIRE(!matrix_sum.success());
    REQUIRE(diagnostic(matrix_sum, "MPF2028"));
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::python;
    options.target = target;
    const auto python = mpf::Transpiler{}.transpile("print(len(1))\n", options);
    REQUIRE(!python.success());
    REQUIRE(diagnostic(python, "MPF2022"));
  }
}

TEST_CASE("Matlab range serialization retains both operands and declaration source maps") {
  const auto javascript = compile(source, mpf::TargetLanguage::javascript);
  const auto cpp = compile(source, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("[4, [27, lower, upper, 3]], 0);") != std::string::npos);
  REQUIRE(
      cpp.code.find("argument_validator_call{27U, "
                    "{mpf_runtime::argument_validator_real_component(lower.value()), "
                    "mpf_runtime::argument_validator_real_component(upper.value())}, 2U, 3U}") !=
      std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    for (const auto line : {5U, 8U}) {
      REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                          [=](const auto& segment) { return segment.original_line == line; }));
    }
  }
}

TEST_CASE("Matlab range syntax and plans reject invalid or inactive boundary state") {
  using namespace mpf::detail;
  ArgumentDeclarationSyntax declaration;
  declaration.name = "value";
  declaration.line = 1U;
  declaration.validators = {{"mustBeInRange", 4U, true}};
  REQUIRE(valid_argument_declaration_syntax(declaration));
  declaration.validators.front().name.clear();
  REQUIRE(!valid_argument_declaration_syntax(declaration));
  declaration.validators.front().name = "customRange";
  REQUIRE(valid_argument_declaration_syntax(declaration));
  declaration.validators.front().explicit_call = false;
  REQUIRE(!valid_argument_declaration_syntax(declaration));
  declaration.validators.front().argument_count = 1U;
  REQUIRE(valid_argument_declaration_syntax(declaration));

  ArgumentValidationPlan plan;
  plan.line = 1U;
  plan.validators = {{ArgumentValidator::in_range,
                      {{ArgumentValidatorOperandKind::numeric_literal, "0.0", dynamic_extent},
                       {ArgumentValidatorOperandKind::numeric_literal, "1.0", dynamic_extent}},
                      ArgumentRangeBoundary::exclusive}};
  plan.validators.front().source_call = HirNodeId{1U};
  plan.validators.front().source_callee = HirNodeId{2U};
  REQUIRE(valid_argument_validation_plan(plan, 1U, 0U));
  inject_range_boundary_byte(plan.validators.front().range_boundary, 255U);
  REQUIRE(!valid_argument_validation_plan(plan, 1U, 0U));
  plan.validators.front().range_boundary = ArgumentRangeBoundary::exclude_upper;
  plan.validators.front().validator = ArgumentValidator::greater_than;
  plan.validators.front().operands.pop_back();
  REQUIRE(!valid_argument_validation_plan(plan, 1U, 0U));
}

TEST_CASE("MIR range contracts independently reject arity and invalid boundary mutations") {
  const auto mir = lower_range();
  REQUIRE(mpf::detail::mir::verify(mir.program, "range").empty());
  REQUIRE(mpf::detail::dump_mir(mir.program).find("27($0,$1):bounds=3") != std::string::npos);
  auto invalid = mir.program;
  auto function = std::find_if(
      invalid.statements.begin(), invalid.statements.end(),
      [](const auto& entry) { return entry.kind == mpf::detail::StatementKind::function; });
  REQUIRE(function != invalid.statements.end());
  auto& call = function->argument_validations[2].validators.back();
  inject_range_boundary_byte(call.range_boundary, 4U);
  REQUIRE(!mpf::detail::mir::verify(invalid, "bad-range-flags").empty());
  call.range_boundary = mpf::detail::ArgumentRangeBoundary::exclusive;
  call.operands.pop_back();
  REQUIRE(!mpf::detail::mir::verify(invalid, "bad-range-arity").empty());
}

TEST_CASE("JavaScript range LIR binds both thresholds and verifies normalized boundary ABI") {
  namespace js = mpf::detail::javascript;
  const auto mir = lower_range();
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
  const auto& call = program->statements.front().plan.argument_validators[2].back();
  REQUIRE(call.opcode == 27U);
  REQUIRE(call.operands.size() == 2U);
  REQUIRE(call.range_boundary == 3U);
  REQUIRE(call.operands[1].token == "upper");
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].back().range_boundary = 0U;
      },
      js::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].back().operands.pop_back();
      },
      js::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        auto& operands = invalid.statements.front().plan.argument_validators[2].back().operands;
        std::swap(operands[0], operands[1]);
      },
      js::verify_lir_representation);
}

TEST_CASE("cpp range LIR plans optional real-component access before serialization") {
  namespace cpp = mpf::detail::cpp;
  const auto mir = lower_range();
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
  const auto& call = program->statements.front().plan.argument_validators[2].back();
  REQUIRE(call.opcode == 27U);
  REQUIRE(call.operands.size() == 2U);
  REQUIRE(call.range_boundary == 3U);
  REQUIRE(call.operands[1].form ==
          cpp::lir::ValidatorOperandForm::optional_parameter_real_component);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].back().range_boundary = 0U;
      },
      cpp::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].back().operands[1].form =
            cpp::lir::ValidatorOperandForm::parameter_real_component;
      },
      cpp::verify_lir_representation);
  require_rejected(
      *program,
      [](auto& invalid) {
        invalid.statements.front().plan.argument_validators[2].back().operands[1].form =
            cpp::lir::ValidatorOperandForm::optional_parameter_value;
      },
      cpp::verify_lir_representation);
}
