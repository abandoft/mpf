#include <algorithm>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <utility>
#include <vector>

#include "frontends/common/registry.hpp"
#include "ir/mir_optimization.hpp"
#include "ir/output_assignment.hpp"
#include "ir/pass_manager.hpp"
#include "mpf/transpiler.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;
using State = mir::OutputAssignmentState;

void require_clean(const std::vector<mpf::Diagnostic>& diagnostics) {
  if (!diagnostics.empty()) throw std::runtime_error(diagnostics.front().message);
}

mir::Program lower(const std::string& source) {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(source, "assignment.m"));
  require_clean(parsed.diagnostics);
  auto hir = matlab_frontend().lower(std::move(parsed.ast));
  require_clean(hir.diagnostics);
  auto analysis = analyze_program(hir.program, std::move(hir.semantics));
  require_clean(analysis.diagnostics);
  auto mir =
      mir::lower_from_hir(std::move(hir.program), std::move(analysis.semantics), analysis.names);
  require_clean(mir.diagnostics);
  require_clean(mir::verify(mir.program, "output-assignment-test"));
  return std::move(mir.program);
}

mir::OutputAssignmentTable analyze(const mir::Program& program) {
  const auto effects = mir::analyze_alias_effects(program);
  auto result = mir::analyze_output_assignments(program, effects);
  REQUIRE(result.complete);
  require_clean(mir::verify_output_assignments(program, effects, result, "output-assignment-test"));
  return result;
}

// Algorithm-only fixtures, not source-language acceptance or native execution
// evidence. They exercise paths that the current Analyzer intentionally rejects
// until executable output-presence guards and both target ABIs are implemented.
class Graph final {
 public:
  explicit Graph(const std::size_t outputs = 2U) {
    program.source_language = mpf::SourceLanguage::matlab;
    program.revision = 1U;
    program.types.resize(2U);
    program.types[1].value_type = ValueType::real;
    program.types[1].numeric_type = real_numeric_type;
    program.shapes.resize(2U);
    program.storages.emplace_back();
    program.instructions.emplace_back();
    program.blocks.emplace_back();
    program.functions.resize(2U);
    program.statements.resize(2U);
    program.attributes.instructions.emplace_back();
    auto& owner = program.statements[1];
    owner.id = MirStatementId{1U};
    owner.origin = HirNodeId{1U};
    owner.kind = StatementKind::function;
    auto& function = program.functions[1];
    function.id = MirFunctionId{1U};
    function.origin = owner.origin;
    function.name = "graph";
    for (std::size_t index = 0U; index < outputs; ++index) {
      const SymbolId symbol{static_cast<SymbolId::value_type>(index + 1U)};
      const auto name = "output" + std::to_string(index);
      owner.return_names.push_back(name);
      owner.return_symbols.push_back(symbol);
      function.result_types.push_back(TypeId{1U});
      function.result_shapes.push_back(ShapeId{1U});
      mir::StorageData storage;
      storage.name = name;
      storage.symbol = symbol;
      storage.origin = owner.origin;
      storage.type = TypeId{1U};
      storage.shape = ShapeId{1U};
      program.storages.push_back(std::move(storage));
    }
  }

  BlockId block() {
    const BlockId id{static_cast<BlockId::value_type>(program.blocks.size())};
    mir::BasicBlock result;
    result.id = id;
    program.blocks.push_back(std::move(result));
    auto& function = program.functions[1];
    function.blocks.push_back(id);
    if (!function.entry.valid()) function.entry = id;
    return id;
  }

  void link(const BlockId block, std::vector<BlockId> targets) {
    auto& terminator = program.blocks[block.value()].terminator;
    terminator.kind =
        targets.size() > 1U ? mir::TerminatorKind::conditional_branch : mir::TerminatorKind::branch;
    terminator.successors = std::move(targets);
    terminator.successor_arguments.resize(terminator.successors.size());
  }

  InstructionId instruction(const BlockId block, const mir::Opcode opcode,
                            const std::size_t output = dynamic_extent) {
    mir::Instruction result;
    result.id = InstructionId{static_cast<InstructionId::value_type>(program.instructions.size())};
    result.opcode = opcode;
    result.origin = HirNodeId{1U};
    result.location = {1U, 1U};
    result.intrinsic = opcode == mir::Opcode::call ? IntrinsicId::matlab_error : IntrinsicId::none;
    mir::InstructionAttributes attributes;
    attributes.origin = result.id;
    if (output != dynamic_extent) {
      result.storage = StorageId{static_cast<StorageId::value_type>(output + 1U)};
      attributes.memory_accesses.push_back(
          {result.storage, result.storage, full_storage_region({}), mir::MemoryAccessMode::write});
    }
    const auto id = result.id;
    program.blocks[block.value()].instructions.push_back(id);
    program.instructions.push_back(std::move(result));
    program.attributes.instructions.push_back(std::move(attributes));
    return id;
  }

  mir::Program program;
};

}  // namespace

TEST_CASE("output assignment lattice preserves path dependence and unreachable identity") {
  for (const auto state :
       {State::unreachable, State::unassigned, State::assigned, State::path_dependent}) {
    REQUIRE(mir::join_output_assignment(State::unreachable, state) == state);
    REQUIRE(mir::join_output_assignment(state, state) == state);
    REQUIRE(mir::join_output_assignment(state, State::invalid) == State::invalid);
  }
  REQUIRE(mir::join_output_assignment(State::assigned, State::unassigned) == State::path_dependent);
  REQUIRE(mir::join_output_assignment(State::path_dependent, State::assigned) ==
          State::path_dependent);
}

TEST_CASE("output assignment follows both sides of a real typed Matlab CFG") {
  auto program = lower(
      "function [first,second] = choose(flag)\nif flag\n"
      "first = 1;\nsecond = 2;\nelse\nfirst = 3;\nsecond = 4;\nend\nend\n");
  const auto result = analyze(program);
  const auto found = std::find_if(program.functions.begin(), program.functions.end(),
                                  [](const auto& function) { return function.name == "choose"; });
  REQUIRE(found != program.functions.end());
  REQUIRE(result.functions[found->id.value()].outputs.size() == 2U);
  REQUIRE(result.block_entry(found->entry, 0U) == State::unassigned);
  REQUIRE(result.block_entry(found->entry, 1U) == State::unassigned);
  bool returned = false;
  for (const auto block : found->blocks) {
    if (program.blocks[block.value()].terminator.kind != mir::TerminatorKind::return_value)
      continue;
    returned = true;
    REQUIRE(result.block_exit(block, 0U) == State::assigned);
    REQUIRE(result.block_exit(block, 1U) == State::assigned);
  }
  REQUIRE(returned);
  require_clean(mir::run_default_optimization_pipeline(program).diagnostics);
  (void)analyze(program);
}

TEST_CASE("output assignment tracks actual normalization of shared input output storage") {
  const auto program = lower(
      "function value = shared(value)\narguments\n"
      "value (1,1) double\nend\narguments (Output)\n"
      "value (1,1) logical\nend\nend\n");
  const auto result = analyze(program);
  const auto found = std::find_if(program.functions.begin(), program.functions.end(),
                                  [](const auto& function) { return function.name == "shared"; });
  REQUIRE(found != program.functions.end());
  const auto& slot = result.functions[found->id.value()].outputs.front();
  REQUIRE(!slot.assigned_at_entry);
  REQUIRE(slot.workspace_type != slot.result_type);
  const auto& entry = found->argument_entries.front();
  REQUIRE(slot.workspace == entry.storage);
  REQUIRE(result.before(entry.initialization, 0U) == State::unassigned);
  REQUIRE(result.after(entry.initialization, 0U) == State::assigned);
  REQUIRE(result.before(found->argument_outputs.front().selection, 0U) == State::assigned);
}

TEST_CASE("shared raw Matlab input output is assigned without a fabricated store") {
  const auto program = lower("function value = shared(value)\nend\n");
  const auto result = analyze(program);
  const auto found = std::find_if(program.functions.begin(), program.functions.end(),
                                  [](const auto& function) { return function.name == "shared"; });
  REQUIRE(found != program.functions.end());
  REQUIRE(result.functions[found->id.value()].outputs.front().assigned_at_entry);
  REQUIRE(result.block_entry(found->entry, 0U) == State::assigned);
  REQUIRE(result.block_exit(found->entry, 0U) == State::assigned);
}

TEST_CASE("conditional writes do not turn a merged output into a definitely assigned output") {
  Graph graph;
  const auto entry = graph.block();
  const auto left = graph.block();
  const auto right = graph.block();
  const auto merge = graph.block();
  graph.link(entry, {left, right});
  graph.link(left, {merge});
  graph.link(right, {merge});
  graph.instruction(left, mir::Opcode::store, 0U);
  graph.instruction(left, mir::Opcode::store, 1U);
  graph.instruction(right, mir::Opcode::store, 0U);
  const auto result = analyze(graph.program);
  REQUIRE(result.block_entry(merge, 0U) == State::assigned);
  REQUIRE(result.block_entry(merge, 1U) == State::path_dependent);
  REQUIRE(result.block_exit(merge, 1U) == State::path_dependent);
}

TEST_CASE("loop assignedness includes zero iterations and repeated backedges") {
  Graph graph(1U);
  const auto entry = graph.block();
  const auto header = graph.block();
  const auto body = graph.block();
  const auto exit = graph.block();
  graph.link(entry, {header});
  graph.link(header, {body, exit});
  graph.link(body, {header});
  const auto store = graph.instruction(body, mir::Opcode::store, 0U);
  const auto result = analyze(graph.program);
  REQUIRE(result.block_entry(header, 0U) == State::path_dependent);
  REQUIRE(result.block_exit(exit, 0U) == State::path_dependent);
  REQUIRE(result.before(store, 0U) == State::path_dependent);
  REQUIRE(result.after(store, 0U) == State::assigned);
}

TEST_CASE("exception edges use each failing instruction pre commit state") {
  Graph graph;
  const auto entry = graph.block();
  const auto handler = graph.block();
  const auto merge = graph.block();
  graph.program.blocks[entry.value()].exception_handler = handler;
  graph.instruction(entry, mir::Opcode::call);
  graph.instruction(entry, mir::Opcode::store, 0U);
  graph.instruction(entry, mir::Opcode::call);
  graph.instruction(entry, mir::Opcode::store, 1U);
  graph.instruction(handler, mir::Opcode::store, 1U);
  graph.link(entry, {merge});
  graph.link(handler, {merge});
  const auto result = analyze(graph.program);
  REQUIRE(result.block_exception(entry, 0U) == State::path_dependent);
  REQUIRE(result.block_exception(entry, 1U) == State::unassigned);
  REQUIRE(result.block_entry(handler, 0U) == State::path_dependent);
  REQUIRE(result.block_entry(handler, 1U) == State::unassigned);
  REQUIRE(result.block_entry(merge, 0U) == State::path_dependent);
  REQUIRE(result.block_entry(merge, 1U) == State::assigned);
}

TEST_CASE("failing indexed stores do not assign an output on the exceptional path") {
  Graph graph(1U);
  const auto entry = graph.block();
  const auto handler = graph.block();
  graph.program.blocks[entry.value()].exception_handler = handler;
  const auto store = graph.instruction(entry, mir::Opcode::store_indexed, 0U);
  const auto effects = mir::analyze_alias_effects(graph.program);
  REQUIRE(mir::has_effect(effects.instruction(store)->effects, mir::Effect::may_fail));
  const auto result = analyze(graph.program);
  REQUIRE(result.before(store, 0U) == State::unassigned);
  REQUIRE(result.after(store, 0U) == State::assigned);
  REQUIRE(result.block_entry(handler, 0U) == State::unassigned);
}

TEST_CASE("unreachable writes pure queries and discarded receivers cannot assign outputs") {
  Graph graph(1U);
  const auto entry = graph.block();
  const auto unreachable = graph.block();
  const auto handler = graph.block();
  graph.program.blocks[entry.value()].exception_handler = handler;
  const auto query = graph.instruction(entry, mir::Opcode::call);
  graph.program.instructions[query.value()].intrinsic = IntrinsicId::matlab_nargout;
  graph.instruction(entry, mir::Opcode::discard_output, 0U);
  const auto store = graph.instruction(unreachable, mir::Opcode::store, 0U);
  const auto result = analyze(graph.program);
  REQUIRE(result.before(store, 0U) == State::unreachable);
  REQUIRE(result.after(store, 0U) == State::unreachable);
  REQUIRE(result.block_exit(entry, 0U) == State::unassigned);
  REQUIRE(result.block_entry(handler, 0U) == State::unreachable);
  REQUIRE(result.writes.size() == 1U);
}

TEST_CASE("unbounded external call effects are not proof of binding initialization") {
  Graph graph(1U);
  const auto entry = graph.block();
  const auto call = graph.instruction(entry, mir::Opcode::call, 0U);
  graph.program.instructions[call.value()].intrinsic = IntrinsicId::none;
  const auto result = analyze(graph.program);
  REQUIRE(result.writes.empty());
  REQUIRE(result.after(call, 0U) == State::unassigned);
}

TEST_CASE("output assignment tables are revision cached deterministic and reentrant") {
  Graph graph;
  const auto entry = graph.block();
  graph.instruction(entry, mir::Opcode::store, 0U);
  AnalysisManager<mir::Program> manager;
  std::size_t computations = 0U;
  const auto compute = [&](const mir::Program& program) {
    ++computations;
    return analyze(program);
  };
  const auto& first =
      manager.get<mir::OutputAssignmentTable>(graph.program, "output-assignment", compute);
  const auto& second =
      manager.get<mir::OutputAssignmentTable>(graph.program, "output-assignment", compute);
  REQUIRE(&first == &second);
  REQUIRE(computations == 1U);
  const auto text = mir::dump_output_assignments(first);
  REQUIRE(text.find("output-assignment-v1") != std::string::npos);
  REQUIRE(text.find("unassigned") != std::string::npos);
  std::vector<std::future<std::string>> parallel;
  parallel.reserve(4U);
  for (std::size_t index = 0U; index < 4U; ++index)
    parallel.push_back(std::async(std::launch::async, [&]() {
      return mir::dump_output_assignments(analyze(graph.program));
    }));
  for (auto& task : parallel) REQUIRE(task.get() == text);
  const auto old = first;
  ++graph.program.revision;
  const auto& fresh =
      manager.get<mir::OutputAssignmentTable>(graph.program, "output-assignment", compute);
  REQUIRE(computations == 2U);
  const auto effects = mir::analyze_alias_effects(graph.program);
  REQUIRE(!mir::output_assignments_current(graph.program, effects, old));
  REQUIRE(mir::output_assignments_current(graph.program, effects, fresh));
}

TEST_CASE("output assignment verifier independently rejects state and provenance corruption") {
  Graph graph;
  const auto entry = graph.block();
  graph.instruction(entry, mir::Opcode::store, 0U);
  const auto effects = mir::analyze_alias_effects(graph.program);
  const auto pristine = analyze(graph.program);
  const std::vector<std::function<void(mir::OutputAssignmentTable&)>> mutations{
      [](auto& value) { value.complete = false; },
      [](auto& value) { ++value.mir_revision; },
      [](auto& value) { ++value.storage_count; },
      [](auto& value) { ++value.instruction_count; },
      [](auto& value) { ++value.block_count; },
      [](auto& value) { ++value.function_count; },
      [](auto& value) { value.functions.pop_back(); },
      [](auto& value) { value.blocks.pop_back(); },
      [](auto& value) { value.instructions.pop_back(); },
      [](auto& value) { value.states.pop_back(); },
      [](auto& value) { value.writes.clear(); },
      [](auto& value) { value.functions[0].origin = MirFunctionId{1U}; },
      [](auto& value) { value.functions[1].owner = {}; },
      [](auto& value) { value.functions[1].outputs.pop_back(); },
      [](auto& value) { value.functions[1].outputs[0].symbol = {}; },
      [](auto& value) { value.functions[1].outputs[0].workspace = {}; },
      [](auto& value) { value.functions[1].outputs[0].result_type = {}; },
      [](auto& value) { value.functions[1].outputs[0].workspace_shape = {}; },
      [](auto& value) { value.functions[1].outputs[0].assigned_at_entry = true; },
      [](auto& value) { value.blocks[1].function = {}; },
      [](auto& value) { value.blocks[1].state_offset = dynamic_extent; },
      [](auto& value) { value.instructions[1].origin = {}; },
      [](auto& value) { value.instructions[1].block = {}; },
      [](auto& value) { value.instructions[1].state_offset = dynamic_extent; },
      [](auto& value) { ++value.instructions[1].write_count; },
      [](auto& value) { ++value.writes[0].output; },
      [](auto& value) { ++value.writes[0].memory_access; },
      [](auto& value) { value.states[0] = State::assigned; },
      [](auto& value) { value.states.back() = State::invalid; }};
  for (const auto& mutation : mutations) {
    auto value = pristine;
    mutation(value);
    REQUIRE(!mir::verify_output_assignments(graph.program, effects, value, "corruption").empty());
  }
  REQUIRE(pristine.block_entry(BlockId{999999U}, 0U) == State::unreachable);
  REQUIRE(pristine.before(InstructionId{}, 0U) == State::unreachable);
  REQUIRE(pristine.after(InstructionId{1U}, 999999U) == State::unreachable);
  auto invalid_dependency = effects;
  invalid_dependency.instructions[1].effects = mir::Effect::none;
  REQUIRE(!mir::verify_output_assignments(graph.program, invalid_dependency, pristine,
                                          "corrupt-effect-dependency")
               .empty());
  invalid_dependency = effects;
  invalid_dependency.storages[1].root = {};
  REQUIRE(!mir::verify_output_assignments(graph.program, invalid_dependency, pristine,
                                          "corrupt-storage-dependency")
               .empty());
}

TEST_CASE("output assignment fixed point agrees with Boolean exploration on random cyclic CFGs") {
  std::uint32_t random = 0x83b29a17U;
  const auto next = [&]() {
    random = random * 1664525U + 1013904223U;
    return random;
  };
  for (std::size_t sample = 0U; sample < 128U; ++sample) {
    Graph graph(4U);
    std::vector<BlockId> blocks;
    blocks.reserve(24U);
    for (std::size_t index = 0U; index < 24U; ++index) blocks.push_back(graph.block());
    for (const auto block : blocks) {
      if (next() % 3U == 0U) graph.instruction(block, mir::Opcode::store, next() % 4U);
      if (next() % 4U == 0U) graph.instruction(block, mir::Opcode::call);
      if (next() % 5U == 0U)
        graph.program.blocks[block.value()].exception_handler = blocks[next() % blocks.size()];
      graph.link(block, {blocks[next() % blocks.size()], blocks[next() % blocks.size()]});
    }
    (void)analyze(graph.program);
  }
}

TEST_CASE("output assignment long CFG stores compact states without per instruction vectors") {
  Graph graph(1U);
  BlockId previous;
  BlockId last;
  for (std::size_t index = 0U; index < 10000U; ++index) {
    const auto block = graph.block();
    if (previous.valid()) graph.link(previous, {block});
    if (index == 5000U) graph.instruction(block, mir::Opcode::store, 0U);
    previous = block;
    last = block;
  }
  const auto result = analyze(graph.program);
  REQUIRE(result.states.size() == 30002U);
  REQUIRE(result.writes.size() == 1U);
  REQUIRE(result.block_exit(last, 0U) == State::assigned);
}

TEST_CASE("never assigned output inventory does not require a fabricated workspace value") {
  Graph graph(1U);
  const auto entry = graph.block();
  graph.program.storages.resize(1U);
  const auto result = analyze(graph.program);
  const auto& output = result.functions[1].outputs.front();
  REQUIRE(!output.workspace.valid());
  REQUIRE(!output.workspace_type.valid());
  REQUIRE(output.result_type.valid());
  REQUIRE(result.block_exit(entry, 0U) == State::unassigned);
}

TEST_CASE(
    "production output assignment analysis does not enable incomplete conditional output "
    "lowering") {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::matlab;
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    options.target = target;
    const auto accepted =
        mpf::Transpiler{}.transpile("function value = checked()\nvalue = 1;\nend\n", options);
    REQUIRE(accepted.success());
    REQUIRE(accepted.report.to_json().find("mir-output-assignment") != std::string::npos);
    const auto conditional = mpf::Transpiler{}.transpile(
        "function value = conditional(flag)\nif flag\nvalue = 1;\nend\nend\n", options);
    REQUIRE(!conditional.success());
    REQUIRE(std::any_of(conditional.diagnostics.begin(), conditional.diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "MPF2004"; }));
  }
}
