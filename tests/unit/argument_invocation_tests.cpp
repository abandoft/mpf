#include <algorithm>
#include <string>
#include <utility>

#include "compiler/argument_validation.hpp"
#include "frontends/common/registry.hpp"
#include "ir/semantic_table.hpp"
#include "mpf/transpiler.hpp"
#include "semantic/analyzer.hpp"
#include "semantic/function_dependencies.hpp"
#include "test_framework.hpp"

namespace {

using namespace mpf::detail;

constexpr auto source =
    "function output = checked(lower,value)\n"
    "arguments\n"
    "lower (1,1) double = 0\n"
    "value (1,1) double {mustBeFinite, mustBeInRange(value,- 0,1e2,'exclude-lower',"
    "\"exclude-upper\"), mustBeGreaterThan(value,lower)} = lower + 1\n"
    "end\n"
    "arguments (Output)\n"
    "output (1,1) double {mustBePositive(output)}\n"
    "end\n"
    "output = value\nend\n";

FrontendParseResult parse(const std::string& text = source) {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(text, "validator_calls.m"));
  REQUIRE(parsed.diagnostics.empty());
  REQUIRE(matlab_frontend().verify(parsed.ast).empty());
  return parsed;
}

hir::LoweringResult lower(const std::string& text = source) {
  auto parsed = parse(text);
  auto lowered = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  return lowered;
}

NameUse& reference(NameTable& names, const HirNodeId id) {
  const auto found = std::find_if(names.uses.begin(), names.uses.end(), [&](const auto& use) {
    return use.origin == id && use.role == NameRole::reference;
  });
  REQUIRE(found != names.uses.end());
  return *found;
}

bool has_diagnostic(const mpf::TranspileResult& result, const std::string& code) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [&](const auto& entry) { return entry.code == code; });
}

}  // namespace

TEST_CASE("Matlab validator invocations own callee formal threshold and flag AST nodes") {
  auto parsed = parse();
  auto& arena = std::get<matlab::ast::Program>(parsed.ast);
  const auto& function = arena.statements[arena.records[arena.roots.front().value()].index];
  REQUIRE(function.argument_validator_calls.size() == 4U);
  const auto expression = [&](const AstNodeId id) -> const matlab::ast::Expression& {
    return arena.expressions[arena.records[id.value()].index];
  };
  const auto& bare = function.argument_validator_calls[0];
  REQUIRE(bare.declaration == 1U);
  REQUIRE(bare.validator == 0U);
  REQUIRE(expression(bare.expression).kind == ExpressionKind::call);
  REQUIRE(expression(bare.expression).children.size() == 2U);
  REQUIRE(expression(expression(bare.expression).children[0]).value == "mustBeFinite");
  REQUIRE(expression(expression(bare.expression).children[1]).value == "value");
  const auto& range = expression(function.argument_validator_calls[1].expression);
  REQUIRE(range.children.size() == 6U);
  REQUIRE(expression(range.children[2]).unary_operation == UnaryOperator::negative);
  // The expression scanner canonicalizes either Matlab quote form to a portable literal.
  REQUIRE(expression(range.children[4]).value == "\"exclude-lower\"");
  REQUIRE(expression(range.children[5]).value == "\"exclude-upper\"");
}

TEST_CASE("AST validator verifier rejects missing reordered foreign and mismatched invocations") {
  for (const auto mutation : {0, 1, 2, 3, 4, 5, 6}) {
    auto parsed = parse();
    auto& arena = std::get<matlab::ast::Program>(parsed.ast);
    auto& function = arena.statements[arena.records[arena.roots.front().value()].index];
    auto& invocation = function.argument_validator_calls[1];
    if (mutation == 0) function.argument_validator_calls.pop_back();
    if (mutation == 1) std::swap(function.argument_validator_calls[0], invocation);
    if (mutation == 2) invocation.expression = AstNodeId{1000000U};
    if (mutation == 3) invocation.declaration = 0U;
    if (mutation >= 4) {
      auto& call = arena.expressions[arena.records[invocation.expression.value()].index];
      if (mutation == 4)
        arena.expressions[arena.records[call.children[0].value()].index].value = "mustBePositive";
      if (mutation == 5)
        arena.expressions[arena.records[call.children[1].value()].index].value = "lower";
      if (mutation == 6)
        arena.expressions[arena.records[call.children[5].value()].index].value = "'inclusive'";
    }
    REQUIRE(!matlab_frontend().verify(parsed.ast).empty());
  }
}

TEST_CASE("HIR validator verifier rejects call ownership operands and duplicate node identities") {
  for (const auto mutation : {0, 1, 2, 3, 4, 5}) {
    auto lowered = lower();
    auto& function = lowered.program.statements.front();
    auto& invocation = function.argument_validator_calls[1];
    if (mutation == 0) function.argument_validator_calls.pop_back();
    if (mutation == 1) invocation.validator = 0U;
    if (mutation == 2) invocation.expression.children[3].value = "2e2";
    if (mutation == 3) invocation.expression.children.front().id = invocation.expression.id;
    if (mutation == 4) invocation.expression.children[2].unary_operation = UnaryOperator::positive;
    if (mutation == 5) lowered.program.language = mpf::SourceLanguage::python;
    REQUIRE(!hir::verify(lowered.program, "validator-invocation").empty());
  }
}

TEST_CASE("all standard Matlab validator candidates have contextual builtin call bindings") {
  for (const auto validator : {ArgumentValidator::numeric,
                               ArgumentValidator::numeric_or_logical,
                               ArgumentValidator::floating,
                               ArgumentValidator::real,
                               ArgumentValidator::finite,
                               ArgumentValidator::non_nan,
                               ArgumentValidator::positive,
                               ArgumentValidator::nonpositive,
                               ArgumentValidator::nonnegative,
                               ArgumentValidator::negative,
                               ArgumentValidator::nonzero,
                               ArgumentValidator::integer,
                               ArgumentValidator::nonempty,
                               ArgumentValidator::scalar_or_empty,
                               ArgumentValidator::vector,
                               ArgumentValidator::row,
                               ArgumentValidator::column,
                               ArgumentValidator::matrix,
                               ArgumentValidator::nonmissing,
                               ArgumentValidator::nonzero_length_text,
                               ArgumentValidator::text,
                               ArgumentValidator::text_scalar,
                               ArgumentValidator::valid_variable_name,
                               ArgumentValidator::greater_than,
                               ArgumentValidator::greater_than_or_equal,
                               ArgumentValidator::less_than,
                               ArgumentValidator::less_than_or_equal,
                               ArgumentValidator::in_range}) {
    const std::string name(argument_validator_name(validator));
    const auto count = argument_validator_operand_count(validator);
    REQUIRE(count.has_value());
    auto call = name + "(value";
    for (std::size_t index = 0U; index < *count; ++index) call += ",0";
    call += ')';
    auto lowered = lower("function output = checked(value)\narguments\nvalue (1,1) double {" +
                         call + "}\nend\noutput = value\nend\n");
    auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
    REQUIRE(analysis.empty());
    const auto& invocation =
        lowered.program.statements.front().argument_validator_calls.front().expression;
    const auto* use = analysis.names.reference(invocation.children.front().id);
    REQUIRE(use != nullptr);
    REQUIRE(use->binding == BindingKind::builtin);
    REQUIRE(!use->symbol.valid());
    REQUIRE(use->intrinsic == IntrinsicId::none);
    REQUIRE(use->argument_validator == validator);
    REQUIRE(analysis.semantics.expression(invocation.id)->argument_validator == validator);
    REQUIRE(analysis.semantics.expression(invocation.children.front().id)->argument_validator ==
            validator);
  }
}

TEST_CASE(
    "validator name resolution honors local function formal output and assignment shadowing") {
  const std::string declaration = "arguments\nvalue (1,1) double {mustBePositive}\nend\n";
  for (const auto& text :
       {"function output = checked(value)\n" + declaration +
            "output = value\nend\n"
            "function mustBePositive(value)\nif value > 0\nerror('positive is "
            "rejected')\nend\nend\n",
        "function output = checked(value,mustBePositive)\n" + declaration + "output = value\nend\n",
        "function mustBePositive = checked(value)\n" + declaration +
            "mustBePositive = value\nend\n",
        "function output = checked(value)\n" + declaration +
            "mustBePositive = 1\noutput = value\nend\n"}) {
    auto lowered = lower(text);
    const auto names = analyze_names(lowered.program);
    REQUIRE(names.diagnostics.empty());
    const auto& callee = lowered.program.statements.front()
                             .argument_validator_calls.front()
                             .expression.children.front();
    const auto* use = names.names.reference(callee.id);
    REQUIRE(use != nullptr);
    REQUIRE(use->symbol.valid());
    REQUIRE(use->binding != BindingKind::builtin);
    REQUIRE(!use->argument_validator.has_value());
    for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
      mpf::TranspileOptions options;
      options.language = mpf::SourceLanguage::matlab;
      options.target = target;
      const auto result = mpf::Transpiler{}.transpile(text, options);
      REQUIRE(!result.success());
      REQUIRE(result.code.empty());
      REQUIRE(has_diagnostic(result, "MPF2062"));
      REQUIRE(!has_diagnostic(result, "MPF0005"));
    }
  }
}

TEST_CASE("name verifier rejects contextual builtin corruption and bypassing validator shadowing") {
  auto lowered = lower();
  auto names = analyze_names(lowered.program);
  REQUIRE(names.diagnostics.empty());
  const auto callee = lowered.program.statements.front()
                          .argument_validator_calls.front()
                          .expression.children.front()
                          .id;
  reference(names.names, callee).argument_validator = ArgumentValidator::positive;
  REQUIRE(!verify_names(lowered.program, names.names, "wrong-validator").empty());
  names = analyze_names(lowered.program);
  reference(names.names, callee).argument_validator.reset();
  REQUIRE(!verify_names(lowered.program, names.names, "missing-validator").empty());
  auto shadowed = lower(
      "function output = checked(value,mustBeFinite)\narguments\n"
      "value (1,1) double {mustBeFinite}\nend\noutput = value\nend\n");
  names = analyze_names(shadowed.program);
  REQUIRE(names.diagnostics.empty());
  auto& use = reference(names.names, shadowed.program.statements.front()
                                         .argument_validator_calls.front()
                                         .expression.children.front()
                                         .id);
  use.symbol = {};
  use.binding = BindingKind::builtin;
  use.argument_validator = ArgumentValidator::finite;
  REQUIRE(!verify_names(shadowed.program, names.names, "shadow-bypass").empty());
}

TEST_CASE("validator source calls participate in identity-resolved function dependencies") {
  auto lowered = lower(
      "function output = checked(value)\narguments\n"
      "value (1,1) double {mustBePositive}\nend\noutput = value\nend\n"
      "function mustBePositive(value)\ndisp(value)\nend\n");
  const auto names = analyze_names(lowered.program);
  REQUIRE(names.diagnostics.empty());
  const auto graph = analyze_function_dependencies(lowered.program, names.names);
  REQUIRE(graph.dependencies.size() == 2U);
  REQUIRE(graph.dependencies[0] == std::vector<std::size_t>{1U});
  REQUIRE(graph.definition_order.front() == 1U);
}

TEST_CASE("semantic validator origins survive reindexing and reject foreign or inactive markers") {
  for (const auto mutation : {0, 1, 2, 3}) {
    auto lowered = lower();
    auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
    REQUIRE(analysis.empty());
    ++lowered.program.revision;
    analysis.semantics = hir::reindex_semantics(lowered.program, std::move(analysis.semantics));
    REQUIRE(
        hir::verify_semantics(lowered.program, analysis.semantics, "validator-reindex").empty());
    const auto& function = lowered.program.statements.front();
    auto* facts = analysis.semantics.statement(function.id);
    const auto& invocation = function.argument_validator_calls.front().expression;
    REQUIRE(facts->argument_validations[1].validators[0].source_call == invocation.id);
    if (mutation == 0) facts->argument_validations[1].validators[0].source_call = function.id;
    if (mutation == 1) facts->argument_validations[1].validators[0].source_callee = invocation.id;
    if (mutation == 2)
      analysis.semantics.expression(invocation.id)->argument_validator =
          ArgumentValidator::positive;
    if (mutation == 3)
      analysis.semantics.expression(function.body.front().expression.id)->argument_validator =
          ArgumentValidator::finite;
    REQUIRE(
        !hir::verify_semantics(lowered.program, analysis.semantics, "corrupt-validator").empty());
  }
}

TEST_CASE("contextual validators do not silently become ordinary global intrinsics") {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::matlab;
  const auto result = mpf::Transpiler{}.transpile("mustBePositive(1)\n", options);
  REQUIRE(!result.success());
  REQUIRE(has_diagnostic(result, "MPF2001"));
}
