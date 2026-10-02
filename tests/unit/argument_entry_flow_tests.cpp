#include <algorithm>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

#include "backends/common/lir_builder.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "ir/dump.hpp"
#include "ir/memory_dependence.hpp"
#include "ir/mir_argument_entry.hpp"
#include "ir/mir_optimization.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

const std::string source =
    "function output = checked(first,second,third)\narguments\n"
    "first (1,1) double {mustBePositive} = 2\n"
    "second (1,1) double {mustBeGreaterThan(second,first)} = first + 1\n"
    "third (1,1) double {mustBeInRange(third,second,second),mustBeLessThan(third,9)} = second\n"
    "end\noutput = first + second + third\nend\n";

mir::Program lower(const std::string& text = source) {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(text, "entry_flow.m"));
  REQUIRE(parsed.diagnostics.empty());
  auto lowered = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.diagnostics.empty());
  auto result = mir::lower_from_hir(std::move(lowered.program), std::move(analysis.semantics),
                                    analysis.names);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(mir::verify(result.program, "entry-flow").empty());
  return std::move(result.program);
}

template <typename Program>
auto& checked(Program& program) {
  const auto found = std::find_if(program.functions.begin(), program.functions.end(),
                                  [](const auto& function) { return function.name == "checked"; });
  REQUIRE(found != program.functions.end());
  return *found;
}

mir::ArgumentOperation& threshold(mir::Program& program) {
  const auto found = std::find_if(program.argument_operations.begin(),
                                  program.argument_operations.end(), [](const auto& operation) {
                                    return operation.kind == mir::ArgumentOperationKind::threshold;
                                  });
  REQUIRE(found != program.argument_operations.end());
  return *found;
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
  if (!diagnostics.empty()) throw std::runtime_error(diagnostics.front().message);
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
  if (!diagnostics.empty()) throw std::runtime_error(diagnostics.front().message);
  return result;
}
}  // namespace

TEST_CASE("Matlab entry MIR separates borrowed raw inputs from normalized typed formal locals") {
  const auto program = lower();
  const auto& function = checked(program);
  const auto& entry = program.blocks[function.entry.value()];
  REQUIRE(function.argument_entries.size() == 3U);
  REQUIRE(function.raw_parameter_types.size() == 3U);
  REQUIRE(function.raw_parameter_shapes.size() == 3U);
  for (std::size_t ordinal = 0U; ordinal < 3U; ++ordinal) {
    const auto& flow = function.argument_entries[ordinal];
    const auto& raw = program.storages[flow.raw_storage.value()];
    const auto& formal = program.storages[flow.storage.value()];
    REQUIRE(raw.kind == mir::StorageKind::parameter);
    REQUIRE(raw.lifetime == mir::StorageLifetime::borrowed);
    REQUIRE(mir::value_type(program, raw.type) == ValueType::unknown);
    REQUIRE(mir::shape(program, raw.shape)->dynamic_rank);
    REQUIRE(formal.kind == mir::StorageKind::local);
    REQUIRE(formal.lifetime == mir::StorageLifetime::function);
    REQUIRE(mir::value_type(program, formal.type) == ValueType::real);
    REQUIRE(formal.symbol == raw.symbol);
    REQUIRE(flow.raw_storage == entry.arguments[ordinal].storage);
    REQUIRE(flow.storage != flow.raw_storage);
    REQUIRE(function.parameter_types[ordinal] == formal.type);
    REQUIRE(function.raw_parameter_types[ordinal] == raw.type);
  }
}

TEST_CASE("Matlab entry MIR orders default selection normalization publication and validation") {
  const auto program = lower();
  const auto& function = checked(program);
  for (std::size_t ordinal = 0U; ordinal < 3U; ++ordinal) {
    const auto& flow = function.argument_entries[ordinal];
    const auto& defaults = function.parameter_defaults[ordinal];
    const auto& block = program.blocks[flow.block.value()];
    REQUIRE(flow.block == defaults.merge_block);
    REQUIRE(flow.selected == defaults.result);
    REQUIRE(block.instructions[0] == flow.normalization);
    REQUIRE(block.instructions[1] == flow.initialization);
    const auto& normalize = program.instructions[flow.normalization.value()];
    REQUIRE(normalize.opcode == mir::Opcode::argument_normalize);
    REQUIRE(normalize.operands == std::vector<ValueId>{flow.selected});
    REQUIRE(program.instructions[flow.initialization.value()].operands ==
            std::vector<ValueId>{normalize.result});
    for (const auto instruction : flow.validators) {
      REQUIRE(program.instructions[instruction.value()].opcode == mir::Opcode::argument_validate);
      REQUIRE(program.instructions[instruction.value()].operands.front() == flow.result);
    }
    REQUIRE(block.terminator.successors == std::vector<BlockId>{flow.continuation});
    REQUIRE(
        defaults.test_block ==
        (ordinal == 0U ? function.entry : function.argument_entries[ordinal - 1U].continuation));
  }
}

TEST_CASE("Matlab entry thresholds are typed resident values or previous normalized formals") {
  auto program = lower();
  const auto& function = checked(program);
  const auto& second = function.argument_entries[1];
  const auto& third = function.argument_entries[2];
  REQUIRE(program.instructions[second.validators[0].value()].operands[1] ==
          function.argument_entries[0].result);
  const auto& range = program.instructions[third.validators[0].value()];
  REQUIRE(range.operands == std::vector<ValueId>({third.result, second.result, second.result}));
  const auto* range_attributes = mir::attributes(program, range.id);
  REQUIRE(range_attributes->memory_accesses.size() == 2U);
  REQUIRE(range_attributes->memory_accesses[1].storage == second.storage);
  const auto& bound = threshold(program);
  const auto& literal = program.instructions[bound.instruction.value()];
  REQUIRE(bound.literal == "9.0");
  REQUIRE(literal.opcode == mir::Opcode::literal);
  REQUIRE(mir::numeric_type(program, literal.type) == real_numeric_type);
  REQUIRE(program.instructions[third.validators[1].value()].operands[1] == literal.result);
  REQUIRE(literal.location.line == 5U);
  REQUIRE(mir::argument_operation(program, third.validators[1])->validator.source_call ==
          literal.origin);
}

TEST_CASE("resident entry operations carry allocation failure and precise memory dependences") {
  const auto program = lower();
  const auto& function = checked(program);
  const auto effects = mir::analyze_alias_effects(program);
  REQUIRE(mir::verify_alias_effects(program, effects, "entry-effects").empty());
  const auto& first = function.argument_entries.front();
  const auto* normalizer = effects.instruction(first.normalization);
  REQUIRE(normalizer != nullptr);
  REQUIRE(mir::has_effect(normalizer->effects, mir::Effect::read));
  REQUIRE(mir::has_effect(normalizer->effects, mir::Effect::allocate));
  REQUIRE(mir::has_effect(normalizer->effects, mir::Effect::may_fail));
  REQUIRE(!mir::has_effect(normalizer->effects, mir::Effect::external_unknown));
  const auto* validator = effects.instruction(first.validators.front());
  REQUIRE(validator != nullptr);
  REQUIRE(mir::has_effect(validator->effects, mir::Effect::may_fail));
  REQUIRE(mir::has_effect(validator->effects, mir::Effect::control));
  const auto dependences = mir::analyze_memory_dependences(program, effects);
  REQUIRE(dependences.complete);
  REQUIRE(mir::verify_memory_dependences(program, effects, dependences, "entry-memory").empty());
  const auto dependent = [&](const InstructionId target) {
    return std::any_of(
        dependences.dependences.begin(), dependences.dependences.end(), [&](const auto& item) {
          return item.source.instruction == first.initialization &&
                 item.target.instruction == target && item.kind == mir::MemoryDependenceKind::flow;
        });
  };
  REQUIRE(dependent(first.validators.front()));
  REQUIRE(dependent(function.argument_entries[1].validators.front()));
}

TEST_CASE("entry operation identities and raw shapes survive optimization and CFG compaction") {
  const std::string prefix =
      "function output = prefix()\nif 1\nend\noutput = (1 + 2) * (3 + 4)\nend\n";
  auto program = lower(prefix + source);
  const auto before = checked(program).argument_entries;
  const auto optimized = mir::run_default_optimization_pipeline(program);
  REQUIRE(optimized.diagnostics.empty());
  REQUIRE(optimized.statistics.removed_instructions > 0U);
  REQUIRE(optimized.statistics.removed_blocks > 0U);
  const auto& after = checked(program).argument_entries;
  REQUIRE(after.front().normalization != before.front().normalization);
  REQUIRE(after.front().block != before.front().block);
  REQUIRE(after.front().result == before.front().result);
  REQUIRE(mir::verify(program, "optimized-entry").empty());
  for (const auto& flow : after) {
    REQUIRE(mir::argument_operation(program, flow.normalization) != nullptr);
    for (const auto instruction : flow.validators)
      REQUIRE(mir::argument_operation(program, instruction) != nullptr);
  }
  auto repeated = lower(prefix + source);
  REQUIRE(mir::run_default_optimization_pipeline(repeated).diagnostics.empty());
  REQUIRE(dump_mir(repeated) == dump_mir(program));
}

TEST_CASE(
    "independent entry verifier rejects storage typed operand order and provenance corruption") {
  const auto pristine = lower();
  const std::vector<std::function<void(mir::Program&, mir::Function&)>> mutations{
      [](auto&, auto& function) { function.argument_entries.clear(); },
      [](auto&, auto& function) { function.raw_parameter_types.clear(); },
      [](auto&, auto& function) { function.raw_parameter_shapes.clear(); },
      [](auto&, auto& function) { function.argument_entries[0].raw_storage = {}; },
      [](auto&, auto& function) { function.argument_entries[0].storage = {}; },
      [](auto&, auto& function) { function.argument_entries[0].parameter = 1U; },
      [](auto&, auto& function) {
        function.argument_entries[0].storage = function.argument_entries[0].raw_storage;
      },
      [](auto& program, auto& function) {
        program.storages[function.argument_entries[0].storage.value()].optional = true;
      },
      [](auto& program, auto& function) {
        program.shapes[function.raw_parameter_shapes[0].value()].dynamic_rank = false;
      },
      [](auto&, auto& function) { function.argument_entries[0].selected = {}; },
      [](auto&, auto& function) { function.argument_entries[0].result = {}; },
      [](auto&, auto& function) { function.argument_entries[0].validators.clear(); },
      [](auto& program, auto& function) {
        auto& instructions =
            program.blocks[function.argument_entries[0].block.value()].instructions;
        std::swap(instructions[0], instructions[1]);
      },
      [](auto& program, auto& function) {
        program.blocks[function.argument_entries[0].block.value()].exception_handler =
            function.argument_entries[1].block;
      },
      [](auto& program, auto& function) {
        program.blocks[function.argument_entries[0].block.value()].terminator.successors[0] =
            function.argument_entries[2].block;
      },
      [](auto& program, auto& function) {
        program.attributes.instructions[function.argument_entries[0].normalization.value()]
            .memory_accesses.clear();
      },
      [](auto& program, auto& function) {
        program.attributes.instructions[function.argument_entries[0].initialization.value()]
            .memory_accesses[0]
            .mode = mir::MemoryAccessMode::read;
      },
      [](auto& program, auto& function) {
        program.instructions[function.argument_entries[1].validators[0].value()].operands[1] =
            function.argument_entries[0].selected;
      },
      [](auto& program, auto& function) {
        program.instructions[function.argument_entries[0].validators[0].value()].operands.clear();
      },
      [](auto& program, auto& function) {
        program.instructions[function.argument_entries[0].validators[0].value()].callee =
            function.id;
      },
      [](auto& program, auto& function) {
        program.attributes.instructions[function.argument_entries[0].normalization.value()]
            .argument_operation = ArgumentOperationId{999999U};
      },
      [](auto& program, auto&) { threshold(program).literal = "8.0"; },
      [](auto& program, auto& function) {
        program.instructions[threshold(program).instruction.value()].type =
            function.raw_parameter_types[0];
      },
      [](auto& program, auto& function) {
        const auto* operation =
            mir::argument_operation(program, function.argument_entries[0].normalization);
        REQUIRE(operation != nullptr);
        const auto id = mir::attributes(program, operation->instruction)->argument_operation;
        program.argument_operations[id.value()].rank = 7U;
      },
      [](auto& program, auto& function) {
        const auto id = mir::attributes(program, function.argument_entries[0].validators[0])
                            ->argument_operation;
        program.argument_operations[id.value()].owner = {};
      },
      [](auto& program, auto& function) { program.argument_operations[0].owner = function.origin; },
      [](auto& program, auto&) {
        program.argument_operations.push_back(program.argument_operations.back());
      }};
  for (const auto& mutate : mutations) {
    auto invalid = pristine;
    mutate(invalid, checked(invalid));
    std::vector<mpf::Diagnostic> diagnostics;
    mir::verify_argument_entries(invalid, diagnostics, "corrupt-entry");
    REQUIRE(!diagnostics.empty());
    REQUIRE(!mir::verify(invalid, "corrupt-entry-full").empty());
  }
}

TEST_CASE("JavaScript and cpp independently legalize the same resident entry sequence") {
  static_assert(!std::is_same_v<javascript::lir::ArgumentEntryPlan, cpp::lir::ArgumentEntryPlan>);
  const auto program = lower();
  auto javascript = javascript_plan(program);
  auto cpp = cpp_plan(program);
  const auto& entries = checked(program).argument_entries;
  REQUIRE(javascript.statements.front().plan.argument_entries.size() == entries.size());
  REQUIRE(cpp.statements.front().plan.argument_entries.size() == entries.size());
  for (std::size_t index = 0U; index < entries.size(); ++index) {
    const auto& js_entry = javascript.statements.front().plan.argument_entries[index];
    const auto& cpp_entry = cpp.statements.front().plan.argument_entries[index];
    REQUIRE(js_entry.form == javascript::lir::ArgumentEntryForm::runtime_normalization);
    REQUIRE(cpp_entry.form == cpp::lir::ArgumentEntryForm::local_materialization);
    REQUIRE(js_entry.source.flow == entries[index]);
    REQUIRE(cpp_entry.source.flow == entries[index]);
    REQUIRE(js_entry.source == cpp_entry.source);
  }
}

TEST_CASE("both target entry verifiers reject stale plans and replanned corrupt MIR projections") {
  const auto program = lower();
  const auto js_clean = javascript_plan(program);
  const auto cpp_clean = cpp_plan(program);
  for (const auto mutation : {0, 1, 2, 3, 4, 5, 6, 7}) {
    auto js = js_clean;
    auto cpp = cpp_clean;
    const auto corrupt = [&](auto& statement) {
      if (mutation == 0) statement.plan.argument_entries.clear();
      if (mutation == 1) statement.plan.argument_entries[0].declaration = 1U;
      if (mutation == 2) statement.source_argument_entries.clear();
      if (mutation == 3)
        statement.source_argument_entries[0].flow.initialization =
            statement.source_argument_entries[0].flow.normalization;
      if (mutation == 4) statement.source_argument_entries[0].rank = 7U;
      if (mutation == 5)
        statement.source_argument_entries[1].validators[0].operands[0].input_ordinal = 1U;
    };
    corrupt(js.statements.front());
    corrupt(cpp.statements.front());
    if (mutation == 6) {
      js.statements.front().plan.argument_entries[0].class_opcode = 0U;
      cpp.statements.front().plan.argument_inputs[0].form = cpp::lir::ArgumentInputForm::direct;
    }
    if (mutation == 7) {
      js.statements.front().plan.argument_entries[0].rank = 7U;
      cpp.statements.front().plan.argument_inputs[0].rank = 7U;
    }
    if (mutation >= 2 && mutation <= 5) {
      javascript::plan_lir_representation(js);
      cpp::plan_lir_representation(cpp);
    }
    std::vector<mpf::Diagnostic> js_diagnostics;
    std::vector<mpf::Diagnostic> cpp_diagnostics;
    javascript::verify_lir_representation(js, js_diagnostics);
    cpp::verify_lir_representation(cpp, cpp_diagnostics);
    REQUIRE(!js_diagnostics.empty());
    REQUIRE(!cpp_diagnostics.empty());
  }
}

TEST_CASE(
    "functions without Matlab input declarations allocate no resident entry operation table") {
  struct PreviousAttributeLayout {
    InstructionId origin;
    std::vector<mir::MemoryAccess> memory_accesses;
  };
  static_assert(sizeof(mir::InstructionAttributes) <= sizeof(PreviousAttributeLayout),
                "sparse argument-operation identity must not enlarge every dense attribute row");
  const auto program = lower("function output = checked(value)\noutput = value + 1\nend\n");
  REQUIRE(program.argument_operations.empty());
  const auto& function = checked(program);
  REQUIRE(function.argument_entries.empty());
  REQUIRE(function.raw_parameter_types.empty());
  REQUIRE(function.raw_parameter_shapes.empty());
}
