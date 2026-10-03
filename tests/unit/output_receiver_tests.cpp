#include <algorithm>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "backends/common/identifier_mangler.hpp"
#include "backends/common/lir_builder.hpp"
#include "backends/common/lir_dump.hpp"
#include "backends/cpp/bindings.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/bindings.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "ir/dump.hpp"
#include "ir/mir_optimization.hpp"
#include "ir/mir_output_receivers.hpp"
#include "ir/semantic_table.hpp"
#include "mpf/transpiler.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

const std::string definition =
    "function [first,second,third] = counted()\n"
    "arguments (Output)\nfirst (1,1) double {mustBePositive}\n"
    "second (1,1) logical\nthird (1,1) double {mustBePositive}\nend\n"
    "first = nargout + 1;\nsecond = nargout;\nthird = nargout + 20;\nend\n";
const std::string source = "[first, ~, third] = counted();\n" + definition;

void require_clean(const std::vector<mpf::Diagnostic>& diagnostics) {
  if (!diagnostics.empty()) throw std::runtime_error(diagnostics.front().message);
}

hir::LoweringResult lower_hir(const std::string& text = source) {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(text, "receivers.m"));
  require_clean(parsed.diagnostics);
  auto lowered = matlab_frontend().lower(std::move(parsed.ast));
  require_clean(lowered.diagnostics);
  return lowered;
}

mir::Program lower(const std::string& text = source) {
  auto hir = lower_hir(text);
  auto analysis = analyze_program(hir.program, std::move(hir.semantics));
  require_clean(analysis.diagnostics);
  auto result =
      mir::lower_from_hir(std::move(hir.program), std::move(analysis.semantics), analysis.names);
  require_clean(result.diagnostics);
  require_clean(mir::verify(result.program, "receivers"));
  return std::move(result.program);
}

javascript::lir::SemanticProgram javascript_plan(const mir::Program& program) {
  auto result = lower_structured_lir<javascript::lir::SemanticProgram, javascript::lir::Statement,
                                     javascript::lir::Expression, javascript::lir::CaseSelector>(
      program, [](HirNodeId, const IntrinsicId id) { return *javascript_code_binding(id); });
  result->runtime.require(javascript::lir::RuntimeFeature::argument_validation);
  result->runtime.require(javascript::lir::RuntimeFeature::arrays);
  result->runtime.require(javascript::lir::RuntimeFeature::complex_numbers);
  result->identifiers =
      allocate_identifiers(mpf::TargetLanguage::javascript, collect_identifier_inventory(*result));
  javascript::plan_lir_resources(*result, mpf::TranspileOptions{});
  javascript::plan_lir_representation(*result);
  std::vector<mpf::Diagnostic> diagnostics;
  javascript::verify_lir_resources(*result, diagnostics);
  javascript::verify_lir_representation(*result, diagnostics);
  require_clean(diagnostics);
  return std::move(*result);
}

cpp::lir::SemanticProgram cpp_plan(const mir::Program& program) {
  auto result = lower_structured_lir<cpp::lir::SemanticProgram, cpp::lir::Statement,
                                     cpp::lir::Expression, cpp::lir::CaseSelector>(
      program, [](HirNodeId, const IntrinsicId id) { return *cpp_code_binding(id); });
  if (std::any_of(program.statements.begin(), program.statements.end(),
                  [](const auto& statement) { return !statement.argument_validations.empty(); }))
    result->runtime.require(cpp::lir::RuntimeFeature::argument_validation);
  result->identifiers =
      allocate_identifiers(mpf::TargetLanguage::cpp, collect_identifier_inventory(*result));
  cpp::plan_lir_resources(*result, mpf::TranspileOptions{});
  cpp::plan_lir_representation(*result);
  std::vector<mpf::Diagnostic> diagnostics;
  cpp::verify_lir_resources(*result, diagnostics);
  cpp::verify_lir_representation(*result, diagnostics);
  require_clean(diagnostics);
  return std::move(*result);
}

template <typename Program, typename Plan, typename Verify>
void reject_target_corruption(const Program& pristine, Plan plan, Verify verify) {
  for (const auto mutation : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18}) {
    auto program = pristine;
    auto& statement = program.statements.front();
    auto& source_receiver = statement.source_receivers[1];
    if (mutation == 0) statement.receivers[1].name = "~";
    if (mutation == 1) source_receiver.owner = {};
    if (mutation == 2) source_receiver.position = 0U;
    if (mutation == 3) source_receiver.kind = OutputReceiverKind::binding;
    if (mutation == 4) source_receiver.instruction = {};
    if (mutation == 5) source_receiver.opcode = mir::Opcode::store;
    if (mutation == 6) source_receiver.storage = statement.source_receivers.front().storage;
    if (mutation == 7) source_receiver.result = statement.source_receivers.front().result;
    if (mutation == 8) source_receiver.argument = ValueId{999999U};
    if (mutation == 9) {
      statement.target_symbols[1] = statement.target_symbols.front();
      source_receiver.symbol = statement.target_symbols.front();
    }
    if (mutation == 10) {
      statement.receivers[1].kind = OutputReceiverKind::binding;
      statement.receivers[1].name = "invented";
    }
    if (mutation == 11) statement.source_receivers.pop_back();
    if (mutation == 12) statement.plan.receivers[2].result_index = 1U;
    if (mutation == 13) statement.target_symbols.pop_back();
    if (mutation == 14) statement.receivers[0].name = "invented";
    if (mutation == 15) statement.receivers[1].location.column += 1U;
    if (mutation == 16)
      for (auto& receiver : statement.source_receivers) receiver.argument = ValueId{999999U};
    if (mutation == 17) statement.plan.receivers[2].location.column += 1U;
    if (mutation == 18) statement.plan.receivers[2].origin = {};
    if (mutation != 12 && mutation != 17 && mutation != 18) plan(program);
    std::vector<mpf::Diagnostic> diagnostics;
    verify(program, diagnostics);
    REQUIRE(!diagnostics.empty());
  }
}

TEST_CASE("generated receiver bindings retain exact source columns in both source maps") {
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::matlab;
    options.target = target;
    const auto result = mpf::Transpiler{}.transpile(source, options);
    require_clean(result.diagnostics);
    REQUIRE(result.success());
    for (const auto column : {2U, 12U})
      REQUIRE(std::any_of(result.source_map.segments.begin(), result.source_map.segments.end(),
                          [&](const auto& segment) {
                            return segment.original_line == 1U && segment.original_column == column;
                          }));
    REQUIRE(result.source_map.to_json() ==
            mpf::Transpiler{}.transpile(source, options).source_map.to_json());
  }
}
}  // namespace

TEST_CASE("Matlab receivers preserve independent kinds positions and exact source columns") {
  auto hir = lower_hir();
  const auto& receivers = hir.program.statements.front().receivers;
  REQUIRE(receivers.size() == 3U);
  REQUIRE(receivers[0].binds());
  REQUIRE(receivers[0].name == "first");
  REQUIRE(receivers[0].location.column == 2U);
  REQUIRE(!receivers[1].binds());
  REQUIRE(receivers[1].name.empty());
  REQUIRE(receivers[1].location.column == 9U);
  REQUIRE(receivers[2].name == "third");
  REQUIRE(receivers[2].location.column == 12U);
  for (const auto& receiver : receivers) REQUIRE(receiver.valid());
  auto whitespace = lower_hir("[first ~ third] = counted();\n" + definition);
  REQUIRE(whitespace.program.statements.front().receivers.size() == 3U);
}

TEST_CASE("Matlab repeated ignored receivers are legal while malformed receiver lists fail") {
  for (const auto prefix : {"[~]", "[~,~]", "[~,~,~]", "[~,second,~]", "[first,~,~]"}) {
    auto hir = lower_hir(std::string(prefix) + " = counted();\n" + definition);
    REQUIRE(!hir.program.statements.front().receivers.empty());
  }
  for (const auto prefix : {"[]", "[,~]", "[~,]", "[~,,first]", "[~first]", "[~~]", "[~+first]",
                            "[first;~]", "[first(1),~]"}) {
    const auto parsed = parse_with_frontend(
        matlab_frontend(), SourceText(std::string(prefix) + " = counted();\n" + definition));
    REQUIRE(!parsed.diagnostics.empty());
  }
}

TEST_CASE("discarded receivers never allocate fake symbols or collapsed binding ordinals") {
  auto hir = lower_hir();
  const auto names = analyze_names(hir.program);
  require_clean(names.diagnostics);
  const auto owner = hir.program.statements.front().id;
  REQUIRE(names.names.use(owner, NameRole::assignment, 0U) != nullptr);
  REQUIRE(names.names.use(owner, NameRole::assignment, 1U) == nullptr);
  REQUIRE(names.names.use(owner, NameRole::assignment, 2U) != nullptr);
  REQUIRE(std::none_of(names.names.symbols.begin(), names.names.symbols.end(),
                       [](const auto& symbol) { return symbol.id.valid() && symbol.name == "~"; }));
  auto analysis = analyze_program(hir.program, std::move(hir.semantics));
  require_clean(analysis.diagnostics);
  const auto* facts = analysis.semantics.statement(owner);
  REQUIRE(facts != nullptr);
  REQUIRE(facts->target_types.size() == 3U);
  REQUIRE(facts->target_types[1] == ValueType::boolean);
  REQUIRE(facts->target_previous_types[1] == ValueType::unknown);
}

TEST_CASE("frontend receiver verification rejects malformed identities and foreign discards") {
  for (const auto mutation : {0, 1, 2, 3, 4}) {
    auto parsed = parse_with_frontend(matlab_frontend(), SourceText(source));
    require_clean(parsed.diagnostics);
    auto& ast = std::get<matlab::ast::Program>(parsed.ast);
    auto& statement = ast.statements[ast.records[ast.roots.front().value()].index];
    if (mutation == 0) statement.receivers[1].name = "invented";
    if (mutation == 1) statement.receivers[1].kind = OutputReceiverKind::invalid;
    if (mutation == 2) statement.receivers[1].location.column = 0U;
    if (mutation == 3) statement.kind = StatementKind::assignment;
    if (mutation == 4) statement.receivers.clear();
    REQUIRE(!verify_frontend_ast(parsed.ast, mpf::SourceLanguage::matlab).empty());
  }
  auto parsed = parse_with_frontend(python_frontend(), SourceText("first,second = (1,2)\n"));
  require_clean(parsed.diagnostics);
  auto& ast = std::get<python::ast::Program>(parsed.ast);
  auto& statement = ast.statements[ast.records[ast.roots.front().value()].index];
  statement.receivers.front().kind = OutputReceiverKind::discard;
  statement.receivers.front().name.clear();
  REQUIRE(!verify_frontend_ast(parsed.ast, mpf::SourceLanguage::python).empty());
}

TEST_CASE("HIR receiver verifier rejects invented discarded previous binding state") {
  auto hir = lower_hir();
  auto analysis = analyze_program(hir.program, std::move(hir.semantics));
  require_clean(analysis.diagnostics);
  require_clean(hir::verify_semantics(hir.program, analysis.semantics, "clean-receivers"));
  const auto owner = hir.program.statements.front().id;
  for (const auto mutation : {0, 1, 2, 3, 4}) {
    auto semantics = analysis.semantics;
    auto* facts = semantics.statement(owner);
    if (mutation == 0) facts->target_previous_types[1] = ValueType::real;
    if (mutation == 1) facts->target_previous_numeric_types[1] = real_numeric_type;
    if (mutation == 2) facts->target_previous_element_types[1] = ValueType::real;
    if (mutation == 3) facts->target_previous_element_numeric_types[1] = real_numeric_type;
    if (mutation == 4) facts->target_previous_array_storage[1] = ArrayStorageFormat::dense;
    REQUIRE(!hir::verify_semantics(hir.program, semantics, "corrupt-receivers").empty());
  }
}

TEST_CASE("MIR receivers consume one shared call and discard without storage results or writes") {
  auto program = lower();
  const auto& statement = program.statements[program.roots.front().value()];
  REQUIRE(statement.receivers.size() == 3U);
  REQUIRE(!statement.target_symbols[1].valid());
  REQUIRE(program.calls.size() == 1U);
  REQUIRE(program.calls.front().requested_results == 3U);
  const auto* expression = mir::expression(program, statement.expression);
  REQUIRE(expression != nullptr);
  const auto effects = mir::analyze_alias_effects(program);
  for (std::size_t index = 0U; index < 3U; ++index) {
    const auto& instruction = program.instructions[statement.instruction.value() + index];
    REQUIRE(instruction.result_index == index);
    REQUIRE(instruction.operands == std::vector<ValueId>{expression->value_id});
    if (index == 1U) {
      REQUIRE(instruction.opcode == mir::Opcode::discard_output);
      REQUIRE(!instruction.storage.valid());
      REQUIRE(!instruction.result.valid());
      REQUIRE(mir::attributes(program, instruction.id)->memory_accesses.empty());
      REQUIRE(effects.instructions[instruction.id.value()].effects.bits() == 0U);
    } else {
      REQUIRE(instruction.opcode == mir::Opcode::store);
      REQUIRE(instruction.storage.valid());
      REQUIRE(instruction.result.valid());
    }
  }
  const auto optimized = mir::run_default_optimization_pipeline(program);
  require_clean(optimized.diagnostics);
  require_clean(mir::verify(program, "optimized-receivers"));
  REQUIRE(program.calls.front().requested_results == 3U);
}

TEST_CASE(
    "MIR verifier rejects invented discard state incorrect slots and mismatched call values") {
  const auto pristine = lower();
  for (const auto mutation : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}) {
    auto program = pristine;
    auto& statement = program.statements[program.roots.front().value()];
    auto& operation = program.instructions[statement.instruction.value() + 1U];
    if (mutation == 0)
      operation.storage = program.instructions[statement.instruction.value()].storage;
    if (mutation == 1)
      operation.result = program.instructions[statement.instruction.value()].result;
    if (mutation == 2) operation.result_index = 0U;
    if (mutation == 3) operation.opcode = mir::Opcode::store;
    if (mutation == 4) operation.operands.clear();
    if (mutation == 5) operation.operands.front() = ValueId{999999U};
    if (mutation == 6) statement.receivers[1].name = "~";
    if (mutation == 7) statement.target_symbols[1] = statement.target_symbols[0];
    if (mutation == 8) statement.target_symbols.clear();
    if (mutation == 9)
      mir::attributes(program, operation.id)->memory_accesses =
          mir::attributes(program, statement.instruction)->memory_accesses;
    if (mutation == 10) operation.callee = MirFunctionId{999999U};
    if (mutation == 11) operation.location.column += 1U;
    if (mutation == 12) {
      auto orphan = operation;
      orphan.id =
          InstructionId{static_cast<InstructionId::value_type>(program.instructions.size())};
      program.instructions.push_back(std::move(orphan));
    }
    if (mutation == 13) program.source_language = mpf::SourceLanguage::python;
    if (mutation == 14) {
      auto* facts = mir::attributes(program, statement.id);
      facts->targets[1].previous_type = facts->targets[1].type;
    }
    std::vector<mpf::Diagnostic> receiver_diagnostics;
    mir::verify_output_receivers(program, receiver_diagnostics, "independent-receivers");
    REQUIRE(!receiver_diagnostics.empty());
    REQUIRE(!mir::verify(program, "corrupt-receivers").empty());
  }
}

TEST_CASE("JavaScript and cpp receivers have private target plans and resident MIR provenance") {
  static_assert(!std::is_same_v<javascript::lir::ReceiverPlan, cpp::lir::ReceiverPlan>);
  const auto program = lower();
  const auto javascript = javascript_plan(program);
  const auto cpp = cpp_plan(program);
  REQUIRE(javascript.statements.front().plan.form ==
          javascript::lir::StatementForm::multi_destructure);
  REQUIRE(cpp.statements.front().plan.form == cpp::lir::StatementForm::multi_tuple);
  REQUIRE(javascript.statements.front().plan.receivers[1].form ==
          javascript::lir::ReceiverForm::discard);
  REQUIRE(cpp.statements.front().plan.receivers[1].form == cpp::lir::ReceiverForm::discard);
  REQUIRE(cpp.statements.front().plan.receivers[2].result_index == 2U);
  reject_target_corruption(javascript, javascript::plan_lir_representation,
                           javascript::verify_lir_representation);
  reject_target_corruption(cpp, cpp::plan_lir_representation, cpp::verify_lir_representation);
}

TEST_CASE("single and all ignored receiver plans avoid unused tuple temporaries and projections") {
  for (const auto prefix : {"[first]", "[~]", "[~,~]", "[~,~,~]"}) {
    const auto program = lower(std::string(prefix) + " = counted();\n" + definition);
    const auto javascript = javascript_plan(program);
    const auto cpp = cpp_plan(program);
    const bool binding = std::string(prefix) == "[first]";
    REQUIRE(javascript.statements.front().plan.form ==
            (binding ? javascript::lir::StatementForm::multi_scalar
                     : javascript::lir::StatementForm::multi_discard));
    REQUIRE(cpp.statements.front().plan.form == (binding ? cpp::lir::StatementForm::multi_scalar
                                                         : cpp::lir::StatementForm::multi_discard));
    if (!binding) {
      REQUIRE(javascript.statements.front().expression.plan.call_value ==
              javascript::lir::CallValueForm::discarded_result);
      REQUIRE(cpp.statements.front().expression.plan.call_value ==
              cpp::lir::CallValueForm::discarded_result);
    }
    if (binding) REQUIRE(cpp.program_scope.declarations.front().tuple_index == dynamic_extent);
    REQUIRE(cpp.temporaries.find(cpp.statements.front().id,
                                 cpp::lir::TemporaryRole::assignment_value) == nullptr);
  }
}

TEST_CASE("parameter-dependent single output types do not probe an already projected tuple twice") {
  const auto program = lower(
      "[value] = pair(19);\n"
      "function [first,second] = pair(input)\nfirst = input;\nsecond = input + 1;\nend\n");
  const auto cpp = cpp_plan(program);
  REQUIRE(cpp.statements.front().expression.plan.call_value ==
          cpp::lir::CallValueForm::first_tuple_result);
  REQUIRE(cpp.program_scope.declarations.front().tuple_index == dynamic_extent);
}

TEST_CASE(
    "single bracket assignments accept scalar procedures and scalar builtins on both targets") {
  const std::string text =
      "[value] = scalar();\ndisp(value);\n[builtin] = sqrt(81);\ndisp(builtin);\n"
      "function output = scalar()\noutput = 7;\nend\n";
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::matlab;
    options.target = target;
    const auto result = mpf::Transpiler{}.transpile(text, options);
    require_clean(result.diagnostics);
    REQUIRE(result.success());
    REQUIRE(result.code.find("std::get<0>") == std::string::npos);
    REQUIRE(result.code == mpf::Transpiler{}.transpile(text, options).code);
    REQUIRE(std::any_of(result.source_map.segments.begin(), result.source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 1U; }));
  }
}

TEST_CASE("ignored positions still require a result and cannot exceed declared output count") {
  for (const auto& text :
       std::vector<std::string>{"[~] = none();\nfunction none()\ndisp(7);\nend\n",
                                "[~,~] = scalar();\nfunction output = scalar()\noutput = 7;\nend\n",
                                "[~,~,~,~] = counted();\n" + definition}) {
    for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
      mpf::TranspileOptions options;
      options.language = mpf::SourceLanguage::matlab;
      options.target = target;
      REQUIRE(!mpf::Transpiler{}.transpile(text, options).success());
    }
  }
}

TEST_CASE("receiver dumps expose kind slots source positions and verified operation provenance") {
  auto hir = lower_hir();
  const auto hir_dump = dump_hir(hir.program);
  REQUIRE(hir_dump.find("1:discard:\"\"@1:9") != std::string::npos);
  const auto bound = lower_hir("[first, second, third] = counted();\n" + definition);
  const auto spaced = lower_hir("[ first , ~ , third ] = counted();\n" + definition);
  REQUIRE(dump_normalized_hir(hir.program) != dump_normalized_hir(bound.program));
  REQUIRE(dump_normalized_hir(hir.program) == dump_normalized_hir(spaced.program));
  const auto program = lower();
  REQUIRE(dump_mir(program).find("output-receivers=") != std::string::npos);
  std::ostringstream javascript_dump;
  std::ostringstream cpp_dump;
  dump_target_lir_body(javascript_dump, javascript_plan(program), "javascript");
  dump_target_lir_body(cpp_dump, cpp_plan(program), "cpp");
  for (const auto& text : {javascript_dump.str(), cpp_dump.str()}) {
    REQUIRE(text.find("receiver-abi [") != std::string::npos);
    REQUIRE(text.find(":storage=!m0:result=%v0") != std::string::npos);
    REQUIRE(text.find(":source=!i") != std::string::npos);
  }
}
