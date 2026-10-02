#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "backends/common/lir_builder.hpp"
#include "backends/common/lir_dump.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "ir/dump.hpp"
#include "ir/memory_dependence.hpp"
#include "ir/mir_argument_exit.hpp"
#include "ir/mir_optimization.hpp"
#include "mpf/transpiler.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

const std::string source =
    "function [first,second] = checked(limit,flag)\narguments\n"
    "limit (1,1) double\nflag (1,1) logical\nend\narguments (Output)\n"
    "first (1,1) double {mustBeLessThan(first,limit),mustBeGreaterThan(first,0)}\n"
    "second (1,1) logical {mustBeNonzero}\nend\n"
    "first = 2;\nsecond = 3;\nlimit = 9;\ntry\nif flag\nreturn;\nend\n"
    "catch\nfirst = 4;\nsecond = 5;\nend\nfirst = 6;\nsecond = 7;\nend\n";

const std::string partial_source =
    "[first,second] = checked();\ndisp(first);\ndisp(second);\n"
    "function [first,second,third] = checked()\narguments (Output)\n"
    "first (1,1) double {mustBePositive}\nsecond (1,1) logical {mustBeNonzero}\n"
    "third (1,2) double {mustBePositive}\nend\n"
    "first = 4;\nsecond = 2;\nthird = [8,9];\nend\n";

mir::Program lower(const std::string& text = source) {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(text, "exit_flow.m"));
  REQUIRE(parsed.diagnostics.empty());
  auto lowered = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
  if (!analysis.diagnostics.empty()) throw std::runtime_error(analysis.diagnostics.front().message);
  auto result = mir::lower_from_hir(std::move(lowered.program), std::move(analysis.semantics),
                                    analysis.names);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(mir::verify(result.program, "exit-flow").empty());
  return std::move(result.program);
}

template <typename Program>
auto& checked(Program& program) {
  const auto found = std::find_if(program.functions.begin(), program.functions.end(),
                                  [](const auto& function) { return function.name == "checked"; });
  REQUIRE(found != program.functions.end());
  return *found;
}

std::vector<mpf::Diagnostic> verify_exit(const mir::Program& program) {
  std::vector<mpf::Diagnostic> diagnostics;
  mir::verify_argument_outputs(program, diagnostics, "independent-exit");
  return diagnostics;
}

template <typename Program, typename Statement, typename Expression, typename Selector>
Program project(const mir::Program& program) {
  auto result = lower_structured_lir<Program, Statement, Expression, Selector>(
      program, [](HirNodeId, IntrinsicId) { return CodeBinding{}; });
  result->source_language = mpf::SourceLanguage::matlab;
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
  javascript::verify_lir_resources(result, diagnostics);
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
  cpp::verify_lir_resources(result, diagnostics);
  REQUIRE(diagnostics.empty());
  return result;
}

template <typename Statement>
Statement& early_return(Statement& root) {
  std::vector<Statement*> pending{&root};
  for (std::size_t index = 0U; index < pending.size(); ++index) {
    auto& node = *pending[index];
    if (node.kind == StatementKind::return_statement) return node;
    for (auto& child : node.body) pending.push_back(&child);
    for (auto& child : node.alternative) pending.push_back(&child);
  }
  throw std::runtime_error("exit fixture has no early return");
}

template <typename Program, typename Verify>
void reject_corrupt_sources(const Program& pristine, Verify verify) {
  for (const auto mutation : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}) {
    auto program = pristine;
    auto& owner = program.statements.front();
    if (mutation == 0) owner.source_argument_exit = {};
    if (mutation == 1) owner.source_argument_outputs.clear();
    if (mutation == 2) owner.source_argument_outputs.front().flow.source_storage = {};
    if (mutation == 3)
      owner.source_argument_outputs.front().flow.storage =
          owner.source_argument_outputs.front().flow.source_storage;
    if (mutation == 4) owner.source_argument_outputs.front().rank = 99U;
    if (mutation == 5) owner.source_argument_outputs.front().validators.clear();
    if (mutation == 6) owner.source_argument_outputs.front().flow.block = {};
    if (mutation == 7) owner.source_argument_exit.returned = {};
    if (mutation == 8) early_return(owner).source_argument_return_exit = {};
    if (mutation == 9) early_return(owner).source_argument_return.origin = owner.origin;
    if (mutation == 10) early_return(owner).plan.argument_return_exit = {};
    if (mutation == 11) owner.plan.argument_exit.source.returns.clear();
    if (mutation == 12) owner.plan.argument_outputs.front().source.flow.selection = {};
    std::vector<mpf::Diagnostic> diagnostics;
    verify(program, diagnostics);
    REQUIRE(!diagnostics.empty());
  }
}
}  // namespace

TEST_CASE("Matlab output MIR sends normal and early returns to one isolated typed boundary") {
  const auto program = lower();
  const auto& function = checked(program);
  const auto& exit = function.argument_exit;
  REQUIRE(exit.returns.size() == 2U);
  REQUIRE(std::count_if(exit.returns.begin(), exit.returns.end(),
                        [](const auto& item) { return item.implicit; }) == 1);
  for (const auto& source_return : exit.returns) {
    const auto& terminator = program.blocks[source_return.block.value()].terminator;
    REQUIRE(terminator.kind == mir::TerminatorKind::branch);
    REQUIRE(terminator.successors == std::vector<BlockId>{exit.merge});
    REQUIRE(terminator.successor_arguments == std::vector<std::vector<ValueId>>{{}});
  }
  REQUIRE(program.blocks[exit.merge.value()].arguments.empty());
  REQUIRE(function.argument_outputs.front().output == 0U);
  REQUIRE(function.argument_outputs.back().output == 1U);
  for (const auto& flow : function.argument_outputs) {
    REQUIRE(!program.blocks[flow.block.value()].exception_handler.valid());
    for (const auto& region : program.exception_regions)
      REQUIRE(std::find(region.protected_blocks.begin(), region.protected_blocks.end(),
                        flow.block) == region.protected_blocks.end());
  }
  const auto& aggregate = program.instructions[exit.aggregation.value()];
  REQUIRE(aggregate.opcode == mir::Opcode::aggregate);
  REQUIRE(aggregate.operands == std::vector<ValueId>({function.argument_outputs[0].result,
                                                      function.argument_outputs[1].result}));
  REQUIRE(aggregate.result == exit.returned);
  REQUIRE(program.blocks[exit.continuation.value()].terminator.operands ==
          std::vector<ValueId>{exit.returned});
}

TEST_CASE("output stages read current workspaces and own separate normalized return storage") {
  const auto program = lower();
  const auto& function = checked(program);
  for (const auto& flow : function.argument_outputs) {
    const auto& block = program.blocks[flow.block.value()];
    REQUIRE(block.instructions[0] == flow.selection);
    REQUIRE(block.instructions[1] == flow.normalization);
    REQUIRE(block.instructions[2] == flow.initialization);
    const auto& selection = program.instructions[flow.selection.value()];
    REQUIRE(selection.opcode == mir::Opcode::identifier);
    REQUIRE(selection.operands.empty());
    REQUIRE(mir::value_type(program, selection.type) == ValueType::unknown);
    REQUIRE(mir::shape(program, selection.shape)->dynamic_rank);
    REQUIRE(selection.storage == flow.source_storage);
    REQUIRE(flow.storage != flow.source_storage);
    const auto& storage = program.storages[flow.storage.value()];
    REQUIRE(storage.kind == mir::StorageKind::temporary);
    REQUIRE(storage.lifetime == mir::StorageLifetime::function);
    REQUIRE(!storage.symbol.valid());
    REQUIRE(storage.type == function.result_types[flow.output]);
    REQUIRE(storage.shape == function.result_shapes[flow.output]);
    REQUIRE(program.instructions[flow.normalization.value()].operands ==
            std::vector<ValueId>{flow.selected});
    REQUIRE(program.instructions[flow.initialization.value()].result == flow.result);
    for (const auto validator : flow.validators)
      REQUIRE(program.instructions[validator.value()].operands.front() == flow.result);
  }
}

TEST_CASE("output thresholds load body-updated normalized inputs rather than entry SSA values") {
  const auto program = lower();
  const auto& function = checked(program);
  const auto& flow = function.argument_outputs.front();
  const auto& validator = program.instructions[flow.validators.front().value()];
  const auto threshold =
      std::find_if(program.instructions.begin(), program.instructions.end(), [&](const auto& item) {
        return item.result.valid() && item.result == validator.operands[1];
      });
  REQUIRE(threshold != program.instructions.end());
  REQUIRE(threshold->opcode == mir::Opcode::identifier);
  REQUIRE(threshold->storage == function.argument_entries.front().storage);
  REQUIRE(threshold->storage != function.argument_entries.front().raw_storage);
  REQUIRE(threshold->result != function.argument_entries.front().result);
  REQUIRE(threshold->type == function.parameter_types.front());
  REQUIRE(mir::argument_operation(program, threshold->id)->direction == ArgumentDirection::output);
}

TEST_CASE("single-result Matlab calls retain the selected normalized type not the callee tuple") {
  const auto program = lower("value = checked(1,true);\ndisp(value);\n" + source);
  const auto& function = checked(program);
  REQUIRE(program.calls.size() == 1U);
  const auto& call = program.calls.front();
  REQUIRE(call.requested_results == 1U);
  REQUIRE(call.result_type == function.result_types.front());
  REQUIRE(mir::value_type(program, call.result_type) == ValueType::real);
  REQUIRE(javascript_plan(program).statements.size() == 3U);
  REQUIRE(cpp_plan(program).statements.size() == 3U);
  for (const auto* declaration : {"(1,1) logical", "(1,2) double"}) {
    const std::string text =
        "first = checked();\nfunction [first,second] = checked()\narguments (Output)\nfirst " +
        std::string(declaration) + "\nsecond (1,1) double\nend\nfirst = 2;\nsecond = 3;\nend\n";
    const auto selected = lower(text);
    REQUIRE(selected.calls.front().result_type == checked(selected).result_types.front());
    REQUIRE(javascript_plan(selected).statements.size() == 2U);
    REQUIRE(cpp_plan(selected).statements.size() == 2U);
    for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
      mpf::TranspileOptions options;
      options.language = mpf::SourceLanguage::matlab;
      options.target = target;
      REQUIRE(mpf::Transpiler{}.transpile(text, options).success());
    }
  }
}

TEST_CASE("output reads stores validators and aggregate participate in alias effect dependence") {
  const auto program = lower();
  const auto effects = mir::analyze_alias_effects(program);
  REQUIRE(mir::verify_alias_effects(program, effects, "exit-effects").empty());
  const auto dependences = mir::analyze_memory_dependences(program, effects);
  REQUIRE(dependences.complete);
  REQUIRE(mir::verify_memory_dependences(program, effects, dependences, "exit-memory").empty());
  for (const auto& flow : checked(program).argument_outputs) {
    const auto* normalization = effects.instruction(flow.normalization);
    REQUIRE(normalization != nullptr);
    REQUIRE(mir::has_effect(normalization->effects, mir::Effect::read));
    REQUIRE(mir::has_effect(normalization->effects, mir::Effect::allocate));
    REQUIRE(mir::has_effect(normalization->effects, mir::Effect::may_fail));
    const auto validator = flow.validators.front();
    REQUIRE(mir::has_effect(effects.instruction(validator)->effects, mir::Effect::may_fail));
    REQUIRE(std::any_of(dependences.dependences.begin(), dependences.dependences.end(),
                        [&](const auto& item) {
                          return item.source.instruction == flow.initialization &&
                                 item.target.instruction == validator &&
                                 item.kind == mir::MemoryDependenceKind::flow;
                        }));
  }
}

TEST_CASE("shared output identities survive value instruction shape and CFG compaction") {
  const std::string prefix =
      "function output = prefix()\nif 1\nend\noutput = (1 + 2) * (3 + 4);\nend\n";
  auto program = lower(prefix + source);
  const auto before = checked(program).argument_exit;
  const auto optimized = mir::run_default_optimization_pipeline(program);
  REQUIRE(optimized.diagnostics.empty());
  REQUIRE(optimized.statistics.removed_instructions > 0U);
  REQUIRE(optimized.statistics.removed_blocks > 0U);
  const auto& after = checked(program).argument_exit;
  REQUIRE(after.merge != before.merge);
  REQUIRE(after.aggregation != before.aggregation);
  REQUIRE(mir::verify(program, "optimized-exit").empty());
  auto repeated = lower(prefix + source);
  REQUIRE(mir::run_default_optimization_pipeline(repeated).diagnostics.empty());
  REQUIRE(dump_mir(program) == dump_mir(repeated));
}

TEST_CASE("independent output verifier rejects bypass exception storage order and operand damage") {
  const auto pristine = lower();
  const std::vector<std::function<void(mir::Program&, mir::Function&)>> mutations{
      [](auto&, auto& function) { function.argument_exit = {}; },
      [](auto&, auto& function) { function.argument_outputs.clear(); },
      [](auto&, auto& function) { function.argument_exit.owner = {}; },
      [](auto&, auto& function) { function.argument_exit.origin = {}; },
      [](auto&, auto& function) { function.argument_exit.returns.clear(); },
      [](auto&, auto& function) {
        function.argument_exit.returns.push_back(function.argument_exit.returns.front());
      },
      [](auto& program, auto& function) {
        program.blocks[function.argument_exit.returns.front().block.value()].terminator.kind =
            mir::TerminatorKind::return_value;
      },
      [](auto& program, auto& function) {
        program.blocks[function.argument_exit.merge.value()].exception_handler = function.entry;
      },
      [](auto& program, auto& function) {
        program.exception_regions.front().protected_blocks.push_back(function.argument_exit.merge);
      },
      [](auto& program, auto& function) {
        program.exception_regions.front().protected_blocks.push_back(
            function.argument_exit.continuation);
      },
      [](auto&, auto& function) { function.argument_outputs.front().output = 1U; },
      [](auto&, auto& function) { function.argument_outputs.front().source_storage = {}; },
      [](auto&, auto& function) {
        function.argument_outputs.front().storage =
            function.argument_outputs.front().source_storage;
      },
      [](auto&, auto& function) { function.argument_outputs.front().selected = {}; },
      [](auto&, auto& function) { function.argument_outputs.front().result = {}; },
      [](auto&, auto& function) { function.argument_outputs.front().initialization = {}; },
      [](auto&, auto& function) { function.argument_outputs.front().validators.clear(); },
      [](auto& program, auto& function) {
        auto& flow = function.argument_outputs.front();
        std::swap(program.blocks[flow.block.value()].instructions[0],
                  program.blocks[flow.block.value()].instructions[1]);
      },
      [](auto& program, auto& function) {
        auto& flow = function.argument_outputs.front();
        program
            .argument_operations[mir::attributes(program, flow.normalization)
                                     ->argument_operation.value()]
            .direction = ArgumentDirection::input;
      },
      [](auto& program, auto& function) {
        auto& flow = function.argument_outputs.front();
        program
            .argument_operations[mir::attributes(program, flow.normalization)
                                     ->argument_operation.value()]
            .rank = 99U;
      },
      [](auto& program, auto& function) {
        auto& flow = function.argument_outputs.front();
        program.attributes.instructions[flow.initialization.value()].memory_accesses.clear();
      },
      [](auto& program, auto& function) {
        const auto& flow = function.argument_outputs.front();
        const auto operation = mir::attributes(program, flow.normalization)->argument_operation;
        program.argument_operations[operation.value()].class_constraint =
            ArgumentClassConstraint::matlab_logical;
      },
      [](auto& program, auto& function) {
        const auto& flow = function.argument_outputs.front();
        program.instructions[flow.selection.value()].type = function.result_types.front();
      },
      [](auto& program, auto& function) {
        const auto& flow = function.argument_outputs.front();
        program.instructions[flow.selection.value()].shape = function.result_shapes.front();
      },
      [](auto& program, auto& function) {
        const auto& flow = function.argument_outputs.front();
        const auto threshold = program.blocks[flow.block.value()].instructions[3];
        program.instructions[threshold.value()].storage =
            function.argument_entries.front().raw_storage;
      },
      [](auto& program, auto& function) {
        const auto& flow = function.argument_outputs.front();
        const auto threshold = program.blocks[flow.block.value()].instructions[5];
        const auto operation = mir::attributes(program, threshold)->argument_operation;
        program.argument_operations[operation.value()].literal = "1.0";
      },
      [](auto& program, auto& function) {
        auto& flow = function.argument_outputs.front();
        program.instructions[flow.validators.front().value()].operands.front() = flow.selected;
      },
      [](auto& program, auto& function) {
        auto& flow = function.argument_outputs.front();
        program.blocks[flow.block.value()].terminator.successors = {
            function.argument_exit.continuation};
      },
      [](auto& program, auto& function) {
        auto& operands = program.instructions[function.argument_exit.aggregation.value()].operands;
        std::swap(operands[0], operands[1]);
      },
      [](auto& program, auto&) {
        program.argument_operations.push_back(program.argument_operations.back());
      }};
  REQUIRE(verify_exit(pristine).empty());
  for (const auto& mutation : mutations) {
    auto program = pristine;
    mutation(program, checked(program));
    REQUIRE(!verify_exit(program).empty());
  }
}

TEST_CASE("JavaScript independently verifies shared output provenance and private return control") {
  const auto program = javascript_plan(lower());
  const auto& owner = program.statements.front();
  REQUIRE(owner.plan.argument_exit.form == javascript::lir::ArgumentExitForm::labeled_scope);
  REQUIRE(owner.plan.argument_outputs.front().source.flow.output == 0U);
  reject_corrupt_sources(program, javascript::verify_lir_representation);
  auto damaged = program;
  damaged.statements.front().plan.argument_exit.form = javascript::lir::ArgumentExitForm::none;
  std::vector<mpf::Diagnostic> diagnostics;
  javascript::verify_lir_representation(damaged, diagnostics);
  REQUIRE(!diagnostics.empty());
}

TEST_CASE("cpp independently verifies shared output provenance and private return control") {
  static_assert(!std::is_same_v<cpp::lir::ArgumentExitPlan, javascript::lir::ArgumentExitPlan>);
  const auto program = cpp_plan(lower());
  const auto& owner = program.statements.front();
  REQUIRE(owner.plan.argument_exit.form == cpp::lir::ArgumentExitForm::labeled_scope);
  REQUIRE(owner.plan.argument_outputs.front().source.flow.output == 0U);
  reject_corrupt_sources(program, cpp::verify_lir_representation);
  auto damaged = program;
  damaged.statements.front().plan.argument_exit.form = cpp::lir::ArgumentExitForm::none;
  std::vector<mpf::Diagnostic> diagnostics;
  cpp::verify_lir_representation(damaged, diagnostics);
  REQUIRE(!diagnostics.empty());
}

TEST_CASE("output exit labels are owned collision-safe resources in both private LIRs") {
  const auto mir = lower();
  auto javascript = javascript_plan(mir);
  auto cpp = cpp_plan(mir);
  const auto corrupt = [](auto& program, const auto role) {
    const auto slot =
        std::find_if(program.temporaries.slots.begin(), program.temporaries.slots.end(),
                     [&](const auto& item) { return item.role == role; });
    REQUIRE(slot != program.temporaries.slots.end());
    REQUIRE(slot->name != "limit");
    const auto other =
        std::find_if(program.temporaries.slots.begin(), program.temporaries.slots.end(),
                     [&](const auto& item) { return item.role != role; });
    REQUIRE(other != program.temporaries.slots.end());
    slot->name = other->name;
  };
  corrupt(javascript, javascript::lir::TemporaryRole::argument_exit);
  corrupt(cpp, cpp::lir::TemporaryRole::argument_exit);
  std::vector<mpf::Diagnostic> javascript_diagnostics;
  std::vector<mpf::Diagnostic> cpp_diagnostics;
  javascript::verify_lir_resources(javascript, javascript_diagnostics);
  cpp::verify_lir_resources(cpp, cpp_diagnostics);
  REQUIRE(!javascript_diagnostics.empty());
  REQUIRE(!cpp_diagnostics.empty());
}

TEST_CASE(
    "output boundary serialization is unique deterministic and maps declarations and returns") {
  const std::string text =
      "function output = checked()\narguments (Output)\noutput (1,1) logical {mustBeNonzero}\n"
      "end\noutput = 1;\ntry\nreturn;\ncatch\noutput = 2;\nend\nend\n";
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::matlab;
    options.target = target;
    options.filename = "exit.m";
    options.emit_source_banner = false;
    const auto result = mpf::Transpiler{}.transpile(text, options);
    REQUIRE(result.success());
    const std::string marker = target == mpf::TargetLanguage::javascript
                                   ? " = __mpf_validate_argument(output,"
                                   : "convert_argument_logical<0>(output,";
    const auto normalization = result.code.find(marker);
    REQUIRE(normalization != std::string::npos);
    REQUIRE(result.code.find(marker, normalization + marker.size()) == std::string::npos);
    for (const auto line : {3U, 7U})
      REQUIRE(std::any_of(result.source_map.segments.begin(), result.source_map.segments.end(),
                          [&](const auto& segment) { return segment.original_line == line; }));
    const auto repeated = mpf::Transpiler{}.transpile(text, options);
    REQUIRE(repeated.success());
    REQUIRE(result.code == repeated.code);
    REQUIRE(result.source_map.to_json() == repeated.source_map.to_json());
  }
}

TEST_CASE("MIR and target dumps expose shared output control storage and provenance") {
  const auto program = lower();
  const auto dump = dump_mir(program);
  REQUIRE(dump.find("mir-v49") != std::string::npos);
  REQUIRE(dump.find("argument-exit owner=") != std::string::npos);
  REQUIRE(dump.find("return-source origin=") != std::string::npos);
  REQUIRE(dump.find("argument-output ordinal=0 workspace=") != std::string::npos);
  REQUIRE(dump.find("direction=1") != std::string::npos);
  const auto javascript = javascript_plan(program);
  const auto cpp = cpp_plan(program);
  std::ostringstream javascript_dump;
  std::ostringstream cpp_dump;
  dump_target_lir_body(javascript_dump, javascript, "javascript");
  dump_target_lir_body(cpp_dump, cpp, "cpp");
  for (const auto& target : {javascript_dump.str(), cpp_dump.str()}) {
    REQUIRE(target.find("semantic-lir-v59") != std::string::npos);
    REQUIRE(target.find("argument-exit-abi ") != std::string::npos);
    REQUIRE(target.find("argument-return-exit ^b") != std::string::npos);
    REQUIRE(target.find(":workspace=!m") != std::string::npos);
  }
}

TEST_CASE("partial Matlab output calls select typed prefixes without truncating the callee") {
  auto program = lower(partial_source);
  const auto& function = checked(program);
  REQUIRE(function.result_types.size() == 3U);
  REQUIRE(function.argument_outputs.size() == 3U);
  REQUIRE(program.calls.size() == 1U);
  const auto& call = program.calls.front();
  REQUIRE(call.requested_results == 2U);
  const auto* selected = mir::type(program, call.result_type);
  REQUIRE(selected != nullptr);
  REQUIRE(selected->kind == mir::TypeKind::tuple);
  REQUIRE(selected->elements ==
          std::vector<TypeId>({function.result_types[0], function.result_types[1]}));
  const auto* expression = mir::expression(program, program.statements[1].expression);
  REQUIRE(expression != nullptr);
  const auto* facts = mir::attributes(program, expression->id);
  REQUIRE(facts != nullptr);
  REQUIRE(facts->tuple_shapes ==
          std::vector<ShapeId>({function.result_shapes[0], function.result_shapes[1]}));
  const auto javascript = javascript_plan(program);
  const auto cpp = cpp_plan(program);
  const std::vector<ValueType> types{ValueType::real, ValueType::boolean};
  REQUIRE(javascript.statements.front().expression.tuple_types == types);
  REQUIRE(cpp.statements.front().expression.tuple_types == types);
  REQUIRE(javascript.statements.front().plan.targets.size() == 2U);
  REQUIRE(cpp.statements.front().plan.targets.size() == 2U);
  REQUIRE(javascript.statements.back().source_argument_outputs.size() == 3U);
  REQUIRE(cpp.statements.back().source_argument_outputs.size() == 3U);
  const auto optimized = mir::run_default_optimization_pipeline(program);
  REQUIRE(optimized.diagnostics.empty());
  REQUIRE(mir::verify(program, "selected-prefix").empty());
  REQUIRE(checked(program).argument_outputs.size() == 3U);
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::matlab;
    options.target = target;
    options.emit_source_banner = false;
    const auto result = mpf::Transpiler{}.transpile(partial_source, options);
    REQUIRE(result.success());
    REQUIRE(std::any_of(result.source_map.segments.begin(), result.source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 1U; }));
    REQUIRE(result.code == mpf::Transpiler{}.transpile(partial_source, options).code);
  }
}

TEST_CASE("MIR rejects call result inventories detached from their resident instruction") {
  auto program = lower(partial_source);
  auto& call = program.calls.front();
  const auto& function = checked(program);
  const auto full_tuple = program.instructions[function.argument_exit.aggregation.value()].type;
  call.result_type = full_tuple;
  call.requested_results = 3U;
  const auto diagnostics = mir::verify(program, "detached-call-result");
  REQUIRE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
    return diagnostic.message.find("call site does not match its call instruction") !=
           std::string::npos;
  }));
}

TEST_CASE("C++ explicitly discards unused projected Matlab results without suppressing calls") {
  const std::string text =
      "checked();\nfunction [first,second] = checked()\narguments (Output)\n"
      "first (1,1) double\nsecond (1,1) logical\nend\n"
      "disp(7);\nfirst = 4;\nsecond = 2;\nend\n";
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::matlab;
  options.target = mpf::TargetLanguage::cpp;
  options.emit_source_banner = false;
  const auto result = mpf::Transpiler{}.transpile(text, options);
  REQUIRE(result.success());
  REQUIRE(result.code.find("static_cast<void>(std::get<0>(checked()));") != std::string::npos);
  REQUIRE(result.code.find("mpf_runtime::print(7)") != std::string::npos);
}
