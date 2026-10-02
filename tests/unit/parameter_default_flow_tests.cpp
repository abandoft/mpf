#include <algorithm>
#include <functional>
#include <string>

#include "backends/common/lir_builder.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "ir/dump.hpp"
#include "ir/memory_dependence.hpp"
#include "ir/mir_optimization.hpp"
#include "ir/mir_parameter_defaults.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

const std::string ordered_defaults =
    "function output = checked(first,second)\narguments\n"
    "first (1,1) double = seed()\nsecond (1,1) double = next(first)\nend\n"
    "output = first + second\nend\n"
    "function output = seed()\ndisp(111)\noutput = 2\nend\n"
    "function output = next(first)\narguments\nfirst (1,1) double\nend\n"
    "disp(222)\noutput = first + 3\nend\n";

mir::Program lower(const std::string& source) {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(source, "default_flow.m"));
  REQUIRE(parsed.diagnostics.empty());
  auto lowered = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.diagnostics.empty());
  auto result = mir::lower_from_hir(std::move(lowered.program), std::move(analysis.semantics),
                                    analysis.names);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(mir::verify(result.program, "test-default-flow").empty());
  return std::move(result.program);
}

template <typename Program>
auto& checked(Program& program) {
  const auto found = std::find_if(program.functions.begin() + 1, program.functions.end(),
                                  [](const auto& function) { return function.name == "checked"; });
  REQUIRE(found != program.functions.end());
  return *found;
}

template <typename Program>
auto& owner(Program& program, const mir::Function& function) {
  const auto found =
      std::find_if(program.statements.begin() + 1, program.statements.end(),
                   [&](const auto& statement) { return statement.origin == function.origin; });
  REQUIRE(found != program.statements.end());
  return *found;
}

std::vector<InstructionId> presence_instructions(const mir::Program& program) {
  std::vector<InstructionId> result;
  for (std::size_t index = 1U; index < program.instructions.size(); ++index)
    if (program.instructions[index].opcode == mir::Opcode::parameter_presence)
      result.emplace_back(static_cast<InstructionId::value_type>(index));
  return result;
}

}  // namespace

TEST_CASE("Matlab default CFG initializes all formals before guarded ordered evaluation") {
  auto program = lower(ordered_defaults);
  const auto& function = checked(program);
  REQUIRE(function.parameter_defaults.size() == 2U);
  const auto& entry = program.blocks[function.entry.value()];
  REQUIRE(entry.arguments.size() == 2U);
  const auto& statement = owner(program, function);
  for (std::size_t parameter = 0U; parameter < 2U; ++parameter) {
    const auto& flow = function.parameter_defaults[parameter];
    const auto& presence = program.instructions[flow.presence.value()];
    REQUIRE(flow.parameter == parameter);
    REQUIRE(flow.source ==
            mir::expression(program, statement.parameter_defaults[parameter])->origin);
    REQUIRE(flow.storage == entry.arguments[parameter].storage);
    REQUIRE(presence.opcode == mir::Opcode::parameter_presence);
    REQUIRE(presence.operands == std::vector<ValueId>{entry.arguments[parameter].value});
    const auto& guard = program.blocks[flow.test_block.value()].terminator;
    REQUIRE(guard.kind == mir::TerminatorKind::conditional_branch);
    REQUIRE(guard.successors[0] == flow.present_block);
    REQUIRE(guard.successors[1] == flow.default_blocks.front());
    REQUIRE(program.blocks[flow.present_block.value()].instructions.empty());
    REQUIRE(program.blocks[flow.default_exit.value()].terminator.successors.front() ==
            flow.merge_block);
    REQUIRE(flow.result != entry.arguments[parameter].value);
    const auto* reads = mir::attributes(program, flow.presence);
    const auto* writes = mir::attributes(program, flow.initialization);
    REQUIRE(reads != nullptr);
    REQUIRE(writes != nullptr);
    REQUIRE(reads->memory_accesses.size() == 1U);
    REQUIRE(writes->memory_accesses.size() == 1U);
    REQUIRE(reads->memory_accesses[0].mode == mir::MemoryAccessMode::read);
    REQUIRE(writes->memory_accesses[0].mode == mir::MemoryAccessMode::write);
  }
  REQUIRE(function.parameter_defaults[0].test_block == function.entry);
  REQUIRE(function.parameter_defaults[1].test_block == function.parameter_defaults[0].merge_block);
  const auto* second = mir::expression(program, statement.parameter_defaults[1]);
  REQUIRE(second != nullptr);
  REQUIRE(second->children.size() == 2U);
  const auto* first_reference = mir::expression(program, second->children[1]);
  REQUIRE(first_reference != nullptr);
  const auto& first_load = program.instructions[first_reference->instruction.value()];
  REQUIRE(first_load.opcode == mir::Opcode::load);
  REQUIRE(first_load.storage == function.parameter_defaults[0].storage);
  // MIR loads read memory, not an SSA value operand. The initialized version is merged on
  // both CFG edges; the independent dependence analysis checks the subsequent storage read.
  REQUIRE(first_load.operands.empty());
  REQUIRE(dump_mir(program).find("parameter-default ordinal=1") != std::string::npos);
}

TEST_CASE("guarded default calls participate in transitive effects and memory dependence") {
  auto program = lower(ordered_defaults);
  const auto& function = checked(program);
  const auto effects = mir::analyze_alias_effects(program);
  REQUIRE(mir::verify_alias_effects(program, effects, "default-effects").empty());
  const auto* facts = effects.function(function.id);
  REQUIRE(facts != nullptr);
  REQUIRE(mir::has_effect(facts->effects, mir::Effect::io));
  for (const auto& flow : function.parameter_defaults) {
    const auto* presence = effects.instruction(flow.presence);
    const auto* initialization = effects.instruction(flow.initialization);
    REQUIRE(presence != nullptr);
    REQUIRE(initialization != nullptr);
    REQUIRE(mir::has_effect(presence->effects, mir::Effect::read));
    REQUIRE(!mir::has_effect(presence->effects, mir::Effect::external_unknown));
    REQUIRE(mir::has_effect(initialization->effects, mir::Effect::write));
    REQUIRE(std::any_of(program.calls.begin(), program.calls.end(), [&](const auto& call) {
      return call.caller == function.id &&
             std::any_of(
                 flow.default_blocks.begin(), flow.default_blocks.end(), [&](const auto block) {
                   const auto& instructions = program.blocks[block.value()].instructions;
                   return std::find(instructions.begin(), instructions.end(), call.instruction) !=
                          instructions.end();
                 });
    }));
  }
  const auto dependencies = mir::analyze_memory_dependences(program, effects);
  REQUIRE(dependencies.complete);
  REQUIRE(mir::verify_memory_dependences(program, effects, dependencies, "default-memory").empty());
  REQUIRE(std::any_of(dependencies.dependences.begin(), dependencies.dependences.end(),
                      [&](const auto& dependence) {
                        return dependence.source.instruction ==
                                   function.parameter_defaults[0].initialization &&
                               dependence.kind == mir::MemoryDependenceKind::flow;
                      }));
}

TEST_CASE("nested short circuit defaults remain entirely within their absence region") {
  auto program = lower(
      "function output = checked(value)\narguments\n"
      "value (1,1) logical = 0 || seed()\nend\noutput = value\nend\n"
      "function output = seed()\ndisp(333)\noutput = 1\nend\n");
  const auto& flow = checked(program).parameter_defaults.front();
  REQUIRE(flow.default_blocks.size() >= 4U);
  REQUIRE(flow.default_exit != flow.default_blocks.front());
  REQUIRE(mir::verify(program, "lazy-default-cfg").empty());
  const auto optimized = mir::run_default_optimization_pipeline(program);
  REQUIRE(optimized.diagnostics.empty());
  REQUIRE(mir::verify(program, "optimized-default-cfg").empty());
  REQUIRE(checked(program).parameter_defaults.size() == 1U);
}

TEST_CASE("default flow identities survive instruction DCE and CFG block compaction") {
  const std::string source =
      "function output = prefix()\nif 1\nend\noutput = (1 + 2) * (3 + 4)\nend\n"
      "function output = checked(first,second)\narguments\n"
      "first (1,1) double = -1\nsecond (1,1) double = first + 3\nend\n"
      "output = first + second\nend\n";
  auto program = lower(source);
  const auto before = checked(program).parameter_defaults;
  const auto optimized = mir::run_default_optimization_pipeline(program);
  REQUIRE(optimized.diagnostics.empty());
  REQUIRE(optimized.statistics.removed_instructions > 0U);
  REQUIRE(optimized.statistics.removed_blocks > 0U);
  const auto& after = checked(program).parameter_defaults;
  REQUIRE(after.size() == before.size());
  REQUIRE(after[0].presence != before[0].presence);
  REQUIRE(after[0].test_block != before[0].test_block);
  REQUIRE(after[0].source == before[0].source);
  REQUIRE(after[0].result == before[0].result);
  REQUIRE(mir::verify(program, "compacted-default-flow").empty());
  const auto effects = mir::analyze_alias_effects(program);
  REQUIRE(mir::verify_alias_effects(program, effects, "compacted-default-effects").empty());
  auto repeated = lower(source);
  REQUIRE(mir::run_default_optimization_pipeline(repeated).diagnostics.empty());
  REQUIRE(dump_mir(repeated) == dump_mir(program));
}

TEST_CASE("default flow verifier rejects guard storage inventory and merge corruption") {
  const auto clean = lower(ordered_defaults);
  const std::vector<std::function<void(mir::Program&, mir::Function&)>> mutations{
      [](auto&, auto& function) { function.parameter_defaults.clear(); },
      [](auto&, auto& function) { function.parameter_defaults[0].source = {}; },
      [](auto&, auto& function) { function.parameter_defaults[0].parameter = 1U; },
      [](auto& program, auto& function) {
        program.storages[function.parameter_defaults[0].storage.value()].optional = false;
      },
      [](auto& program, auto& function) {
        auto& flow = function.parameter_defaults[0];
        program.instructions[flow.presence.value()].operands[0] =
            program.blocks[function.entry.value()].arguments[1].value;
      },
      [](auto& program, auto& function) {
        auto& flow = function.parameter_defaults[0];
        auto& successors = program.blocks[flow.test_block.value()].terminator.successors;
        std::swap(successors[0], successors[1]);
      },
      [](auto&, auto& function) {
        function.parameter_defaults[0].default_blocks.push_back(
            function.parameter_defaults[0].default_blocks.front());
      },
      [](auto& program, auto& function) {
        const auto& flow = function.parameter_defaults[0];
        program.blocks[flow.present_block.value()].terminator.successors[0] =
            flow.default_blocks.front();
      },
      [](auto& program, auto& function) {
        const auto& flow = function.parameter_defaults[0];
        program.instructions[flow.initialization.value()].storage =
            function.parameter_defaults[1].storage;
      },
      [](auto& program, auto& function) {
        const auto& flow = function.parameter_defaults[0];
        program.blocks[flow.default_exit.value()].terminator.successor_arguments[0][0] =
            program.instructions[flow.presence.value()].operands[0];
      },
      [](auto&, auto& function) {
        function.parameter_defaults[1].presence = function.parameter_defaults[0].presence;
      },
      [](auto& program, auto& function) {
        auto& statement = owner(program, function);
        const auto instruction = statement.instruction;
        auto& merge =
            program.blocks[function.parameter_defaults.back().merge_block.value()].instructions;
        merge.erase(std::find(merge.begin(), merge.end(), instruction));
        program.blocks[function.entry.value()].instructions.push_back(instruction);
      }};
  for (const auto& mutate : mutations) {
    auto invalid = clean;
    mutate(invalid, checked(invalid));
    std::vector<mpf::Diagnostic> diagnostics;
    mir::verify_parameter_defaults(invalid, presence_instructions(invalid), diagnostics,
                                   "corrupt-default-flow");
    REQUIRE(!diagnostics.empty());
    REQUIRE(std::all_of(diagnostics.begin(), diagnostics.end(),
                        [](const auto& item) { return item.code == "MPF0006"; }));
    REQUIRE(!mir::verify(invalid, "corrupt-default-flow").empty());
  }
}

TEST_CASE("default verifier rejects orphan presence even when all descriptors are removed") {
  auto program = lower(ordered_defaults);
  auto& function = checked(program);
  auto& statement = owner(program, function);
  function.parameter_defaults.clear();
  statement.parameter_defaults.clear();
  std::vector<mpf::Diagnostic> diagnostics;
  mir::verify_parameter_defaults(program, presence_instructions(program), diagnostics,
                                 "orphan-presence");
  REQUIRE(!diagnostics.empty());
}

TEST_CASE("default verifier terminates on cyclic default expression ownership") {
  auto program = lower(ordered_defaults);
  const auto& function = checked(program);
  const auto& statement = owner(program, function);
  const auto root = statement.parameter_defaults[0];
  program.expressions[root.value()].children.push_back(root);
  std::vector<mpf::Diagnostic> diagnostics;
  mir::verify_parameter_defaults(program, presence_instructions(program), diagnostics,
                                 "cyclic-default");
  REQUIRE(!diagnostics.empty());
  REQUIRE(!mir::verify(program, "cyclic-default").empty());
}

TEST_CASE("JavaScript default lowering independently binds the verified MIR absence flow") {
  const auto program = lower(ordered_defaults);
  const auto resolve = [](const HirNodeId, const IntrinsicId) { return CodeBinding{}; };
  auto lir = lower_structured_lir<javascript::lir::SemanticProgram, javascript::lir::Statement,
                                  javascript::lir::Expression, javascript::lir::CaseSelector>(
      program, resolve);
  lir->source_language = mpf::SourceLanguage::matlab;
  lir->runtime.require(javascript::lir::RuntimeFeature::argument_validation);
  lir->runtime.require(javascript::lir::RuntimeFeature::arrays);
  lir->runtime.require(javascript::lir::RuntimeFeature::complex_numbers);
  javascript::plan_lir_resources(*lir, mpf::TranspileOptions{});
  javascript::plan_lir_representation(*lir);
  std::vector<mpf::Diagnostic> diagnostics;
  javascript::verify_lir_representation(*lir, diagnostics);
  REQUIRE(diagnostics.empty());
  REQUIRE(lir->statements[0].plan.default_flows.size() == 2U);
  for (std::size_t index = 0U; index < 2U; ++index) {
    const auto& plan = lir->statements[0].plan.default_flows[index];
    REQUIRE(plan.form == javascript::lir::ParameterDefaultForm::undefined_guard);
    REQUIRE(plan.source.parameter == index);
    REQUIRE(plan.source.source == lir->statements[0].parameter_defaults[index].origin);
    REQUIRE(plan.source.presence == checked(program).parameter_defaults[index].presence);
  }
  const auto clean = *lir;
  lir->statements[0].plan.default_flows[0].form = javascript::lir::ParameterDefaultForm::none;
  javascript::verify_lir_representation(*lir, diagnostics);
  REQUIRE(!diagnostics.empty());
  *lir = clean;
  diagnostics.clear();
  lir->statements[0].source_parameter_defaults[0].source = {};
  javascript::plan_lir_representation(*lir);
  javascript::verify_lir_representation(*lir, diagnostics);
  REQUIRE(!diagnostics.empty());
  *lir = clean;
  diagnostics.clear();
  lir->statements[0].source_parameter_defaults.clear();
  javascript::plan_lir_representation(*lir);
  javascript::verify_lir_representation(*lir, diagnostics);
  REQUIRE(!diagnostics.empty());
}

TEST_CASE(
    "cpp default lowering independently selects optional resolution and rejects stale provenance") {
  const auto program = lower(ordered_defaults);
  const auto resolve = [](const HirNodeId, const IntrinsicId) { return CodeBinding{}; };
  auto lir = lower_structured_lir<cpp::lir::SemanticProgram, cpp::lir::Statement,
                                  cpp::lir::Expression, cpp::lir::CaseSelector>(program, resolve);
  lir->source_language = mpf::SourceLanguage::matlab;
  lir->runtime.require(cpp::lir::RuntimeFeature::argument_validation);
  cpp::plan_lir_resources(*lir, mpf::TranspileOptions{});
  cpp::plan_lir_representation(*lir);
  std::vector<mpf::Diagnostic> diagnostics;
  cpp::verify_lir_representation(*lir, diagnostics);
  REQUIRE(diagnostics.empty());
  REQUIRE(lir->statements[0].plan.default_flows.size() == 2U);
  REQUIRE(lir->statements[0].plan.default_flows[0].form ==
          cpp::lir::ParameterDefaultForm::optional_resolve);
  const auto clean = *lir;
  lir->statements[0].plan.default_flows[0].source.result = {};
  cpp::verify_lir_representation(*lir, diagnostics);
  REQUIRE(!diagnostics.empty());
  *lir = clean;
  diagnostics.clear();
  lir->statements[0].source_parameter_defaults[0].initialization =
      lir->statements[0].source_parameter_defaults[0].presence;
  cpp::plan_lir_representation(*lir);
  cpp::verify_lir_representation(*lir, diagnostics);
  REQUIRE(!diagnostics.empty());
  *lir = clean;
  diagnostics.clear();
  std::swap(lir->statements[0].source_parameter_defaults[0],
            lir->statements[0].source_parameter_defaults[1]);
  cpp::plan_lir_representation(*lir);
  cpp::verify_lir_representation(*lir, diagnostics);
  REQUIRE(!diagnostics.empty());
}
