#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "frontends/common/registry.hpp"
#include "ir/control_value_join.hpp"
#include "ir/mir_optimization.hpp"
#include "ir/semantic_table.hpp"
#include "mpf/transpiler.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;
const std::string mutable_source =
    "for mode = 0:1\nvalue = 7;\ntry\nvalue = [11,13];\n"
    "if mode == 1\nerror('MPF:Join','body');\nend\n"
    "catch exception\nvalue = 19;\nend\ndisp(numel(value));\nend\n";

mir::Program lower(const std::string& source) {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(source, "mutable_join.m"));
  REQUIRE(parsed.diagnostics.empty());
  auto hir = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(hir.diagnostics.empty());
  auto analysis = analyze_program(hir.program, std::move(hir.semantics));
  REQUIRE(analysis.diagnostics.empty());
  auto result =
      mir::lower_from_hir(std::move(hir.program), std::move(analysis.semantics), analysis.names);
  if (!result.diagnostics.empty()) throw std::runtime_error(result.diagnostics.front().message);
  REQUIRE(mir::verify(result.program, "mutable-join").empty());
  return std::move(result.program);
}

// Algorithm fixtures exercise SSA dependency graphs, not source-language acceptance.
class Graph {
 public:
  Graph() {
    program.types.resize(4U);
    program.types[1].value_type = ValueType::real;
    program.types[1].numeric_type = real_numeric_type;
    program.types[2].kind = mir::TypeKind::sequence;
    program.types[2].value_type = ValueType::list;
    program.types[2].element_type = ValueType::real;
    program.types[2].element_numeric_type = real_numeric_type;
    program.types[2].numeric_type = no_numeric_type;
    program.types[2].array_storage = ArrayStorageFormat::dense;
    program.types[3].value_type = ValueType::boolean;
    program.types[3].numeric_type = logical_numeric_type;
    program.shapes = {{},
                      {{}, {}, semantic::IndexLayout::row_major, false},
                      {{1U, 2U}, {2U, 1U}, semantic::IndexLayout::row_major, false},
                      {{1U, 4U}, {4U, 1U}, semantic::IndexLayout::row_major, false},
                      {{2U, 2U, 2U}, {4U, 2U, 1U}, semantic::IndexLayout::row_major, false}};
    program.storages.resize(2U);
    program.storages[1].type = TypeId{1U};
    program.storages[1].shape = ShapeId{1U};
    program.blocks.emplace_back();
    program.instructions.emplace_back();
  }

  ValueId anchor(const TypeId type, const ShapeId shape) {
    mir::Instruction instruction;
    instruction.id =
        InstructionId{static_cast<InstructionId::value_type>(program.instructions.size())};
    instruction.opcode = mir::Opcode::literal;
    instruction.type = type;
    instruction.shape = shape;
    instruction.result = values.next();
    const auto result = instruction.result;
    program.instructions.push_back(std::move(instruction));
    return result;
  }

  std::size_t phi(const std::vector<ValueId>& inputs = {}) {
    mir::BasicBlock block;
    block.id = BlockId{static_cast<BlockId::value_type>(program.blocks.size())};
    block.arguments.push_back({values.next(), TypeId{1U}, ShapeId{1U}, StorageId{1U}});
    const auto result = program.blocks.size();
    program.blocks.push_back(std::move(block));
    for (const auto input : inputs) incoming(result, input);
    return result;
  }

  void incoming(const std::size_t target, const ValueId input) {
    mir::BasicBlock predecessor;
    predecessor.id = BlockId{static_cast<BlockId::value_type>(program.blocks.size())};
    predecessor.terminator.kind = mir::TerminatorKind::branch;
    predecessor.terminator.successors = {program.blocks[target].id};
    predecessor.terminator.successor_arguments = {{input}};
    program.blocks.push_back(std::move(predecessor));
  }

  ValueId value(const std::size_t block) const {
    return program.blocks[block].arguments.front().value;
  }
  mir::BlockArgument& argument(const std::size_t block) {
    return program.blocks[block].arguments.front();
  }

  void normalize() { mir::normalize_control_value_joins(program); }
  bool verified() const {
    std::vector<mpf::Diagnostic> diagnostics;
    mir::verify_control_value_joins(program, diagnostics, "graph-test");
    return diagnostics.empty();
  }

  mir::Program program;
  IrIdAllocator<ValueId> values;
};
}  // namespace

TEST_CASE("mutable Matlab catch joins reach JavaScript without a public MIR type failure") {
  auto program = lower(mutable_source);
  REQUIRE(mir::run_default_optimization_pipeline(program).diagnostics.empty());
  REQUIRE(mir::verify(program, "optimized-mutable-join").empty());
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::matlab;
  options.target = mpf::TargetLanguage::javascript;
  const auto javascript = mpf::Transpiler{}.transpile(mutable_source, options);
  REQUIRE(javascript.success());
  options.target = mpf::TargetLanguage::cpp;
  const auto cpp = mpf::Transpiler{}.transpile(mutable_source, options);
  REQUIRE(!cpp.success());
  REQUIRE(std::any_of(cpp.diagnostics.begin(), cpp.diagnostics.end(),
                      [](const auto& item) { return item.code == "MPF2007"; }));
  REQUIRE(std::none_of(cpp.diagnostics.begin(), cpp.diagnostics.end(),
                       [](const auto& item) { return item.code == "MPF0006"; }));
}

TEST_CASE("mixed binding storage facts widen without weakening independent semantic validation") {
  const auto source = mutable_source +
                      "value = 0;\nfor step = 1:3\n"
                      "value = [step, step + 1, step + 2];\nend\ndisp(numel(value));\n";
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(source, "mutable_join.m"));
  REQUIRE(parsed.diagnostics.empty());
  auto hir = matlab_frontend().lower(std::move(parsed.ast));
  auto analysis = analyze_program(hir.program, std::move(hir.semantics));
  REQUIRE(analysis.diagnostics.empty());
  REQUIRE(hir::verify_semantics(hir.program, analysis.semantics, "analysis").empty());
  auto found = std::find_if(analysis.semantics.expressions.begin(),
                            analysis.semantics.expressions.end(), [](const auto& facts) {
                              return facts.inferred_type == ValueType::unknown &&
                                     facts.array_storage == ArrayStorageFormat::unknown;
                            });
  REQUIRE(found != analysis.semantics.expressions.end());
  found->array_storage = ArrayStorageFormat::dense;
  REQUIRE(!hir::verify_semantics(hir.program, analysis.semantics, "analysis").empty());
}

TEST_CASE("C++ rejects sequential array rank rebinding at its own capability boundary") {
  const std::string source = "value = [1, 2];\nvalue = [3; 4];\ndisp(value(2));\n";
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::matlab;
  REQUIRE(mpf::Transpiler{}.transpile(source, options).success());
  options.target = mpf::TargetLanguage::cpp;
  const auto result = mpf::Transpiler{}.transpile(source, options);
  REQUIRE(!result.success());
  REQUIRE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                      [](const auto& diagnostic) { return diagnostic.code == "MPF2007"; }));
  REQUIRE(std::none_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [](const auto& diagnostic) { return diagnostic.code == "MPF0006"; }));
}

TEST_CASE(
    "control joins preserve actual homogeneous array contracts instead of initial scalar storage") {
  Graph graph;
  const auto first = graph.anchor(TypeId{2U}, ShapeId{2U});
  const auto second = graph.anchor(TypeId{2U}, ShapeId{2U});
  const auto merged = graph.phi({first, second});
  graph.normalize();
  REQUIRE(graph.argument(merged).type == TypeId{2U});
  REQUIRE(graph.argument(merged).shape == ShapeId{2U});
  REQUIRE(graph.program.storages[1].type == TypeId{1U});
  REQUIRE(graph.verified());
}

TEST_CASE("control joins retain common rank and fixed axes while widening only changing extents") {
  Graph graph;
  const auto merged =
      graph.phi({graph.anchor(TypeId{2U}, ShapeId{2U}), graph.anchor(TypeId{2U}, ShapeId{3U})});
  graph.normalize();
  REQUIRE(graph.argument(merged).type == TypeId{2U});
  const auto* shape = mir::shape(graph.program, graph.argument(merged).shape);
  REQUIRE(shape != nullptr);
  REQUIRE(!shape->dynamic_rank);
  REQUIRE(shape->extents == std::vector<std::size_t>({1U, dynamic_extent}));
  REQUIRE(shape->strides == std::vector<std::size_t>({dynamic_extent, 1U}));
  REQUIRE(graph.verified());
}

TEST_CASE("different control-flow value kinds and ranks join into an explicit dynamic domain") {
  Graph graph;
  const auto merged =
      graph.phi({graph.anchor(TypeId{1U}, ShapeId{1U}), graph.anchor(TypeId{2U}, ShapeId{4U})});
  graph.normalize();
  REQUIRE(mir::value_type(graph.program, graph.argument(merged).type) == ValueType::unknown);
  REQUIRE(mir::shape(graph.program, graph.argument(merged).shape)->dynamic_rank);
  REQUIRE(graph.verified());
  const auto revision = graph.program.revision;
  const auto type_count = graph.program.types.size();
  const auto shape_count = graph.program.shapes.size();
  graph.normalize();
  REQUIRE(graph.program.revision == revision);
  REQUIRE(graph.program.types.size() == type_count);
  REQUIRE(graph.program.shapes.size() == shape_count);
}

TEST_CASE(
    "loop-carried control joins converge and independent SCC proof rejects narrowed contracts") {
  Graph graph;
  const auto first = graph.phi({graph.anchor(TypeId{1U}, ShapeId{1U})});
  const auto second = graph.phi({graph.anchor(TypeId{2U}, ShapeId{2U}), graph.value(first)});
  graph.incoming(first, graph.value(second));
  graph.normalize();
  REQUIRE(graph.verified());
  REQUIRE(mir::value_type(graph.program, graph.argument(first).type) == ValueType::unknown);
  graph.argument(first).type = TypeId{1U};
  REQUIRE(!graph.verified());
  graph.normalize();
  REQUIRE(graph.verified());
  graph.argument(second).shape = ShapeId{2U};
  REQUIRE(!graph.verified());
}

TEST_CASE(
    "control join verifier independently rejects missing SSA definitions and shape pollution") {
  Graph graph;
  const auto merged =
      graph.phi({graph.anchor(TypeId{2U}, ShapeId{2U}), graph.anchor(TypeId{2U}, ShapeId{3U})});
  graph.normalize();
  REQUIRE(graph.verified());
  graph.argument(merged).shape = ShapeId{2U};
  REQUIRE(!graph.verified());
  graph.normalize();
  graph.incoming(merged, ValueId{4000000000U});
  REQUIRE(!graph.verified());
}

TEST_CASE(
    "reverse ten-thousand-phi dependencies use bounded worklist and iterative SCC verification") {
  Graph graph;
  const auto anchor = graph.anchor(TypeId{2U}, ShapeId{3U});
  std::vector<std::size_t> blocks;
  blocks.reserve(10000U);
  for (std::size_t index = 0U; index < 10000U; ++index) blocks.push_back(graph.phi());
  for (std::size_t index = 0U; index + 1U < blocks.size(); ++index)
    graph.incoming(blocks[index], graph.value(blocks[index + 1U]));
  graph.incoming(blocks.back(), anchor);
  graph.normalize();
  REQUIRE(graph.verified());
  for (const auto block : blocks) {
    REQUIRE(graph.argument(block).type == TypeId{2U});
    REQUIRE(graph.argument(block).shape == ShapeId{3U});
  }
}

TEST_CASE("control joins preserve physical strides and widen incompatible layouts") {
  Graph graph;
  graph.program.shapes.push_back({{1U, 2U}, {9U, 3U}, semantic::IndexLayout::row_major, false});
  graph.program.shapes.push_back({{1U, 2U}, {1U, 1U}, semantic::IndexLayout::column_major, false});
  const auto strided =
      graph.phi({graph.anchor(TypeId{2U}, ShapeId{5U}), graph.anchor(TypeId{2U}, ShapeId{5U})});
  const auto layout =
      graph.phi({graph.anchor(TypeId{2U}, ShapeId{2U}), graph.anchor(TypeId{2U}, ShapeId{6U})});
  graph.normalize();
  REQUIRE(graph.argument(strided).shape == ShapeId{5U});
  REQUIRE(mir::shape(graph.program, graph.argument(layout).shape)->dynamic_rank);
  REQUIRE(graph.verified());
  graph.argument(strided).shape = ShapeId{2U};
  REQUIRE(!graph.verified());
}

TEST_CASE("random control joins agree with independent reachable-anchor set oracle") {
  std::uint32_t random = 0x61f2713U;
  const auto next = [&random]() {
    random = random * 1664525U + 1013904223U;
    return random;
  };
  for (std::size_t trial = 0U; trial < 128U; ++trial) {
    Graph graph;
    const std::vector<TypeId> types = {TypeId{1U}, TypeId{2U}, TypeId{2U}, TypeId{3U}};
    const std::vector<ShapeId> shapes = {ShapeId{1U}, ShapeId{2U}, ShapeId{3U}, ShapeId{1U}};
    std::vector<ValueId> anchors;
    anchors.reserve(types.size());
    for (std::size_t index = 0U; index < types.size(); ++index)
      anchors.push_back(graph.anchor(types[index], shapes[index]));
    const auto size = static_cast<std::size_t>(8U + next() % 17U);
    std::vector<std::size_t> blocks;
    blocks.reserve(size);
    std::vector<std::uint32_t> reachable(size);
    std::vector<std::vector<std::size_t>> dependencies(size);
    for (std::size_t index = 0U; index < size; ++index) blocks.push_back(graph.phi());
    for (std::size_t index = 0U; index < size; ++index) {
      const auto anchor = static_cast<std::size_t>(next() % 4U);
      reachable[index] = 1U << anchor;
      graph.incoming(blocks[index], anchors[anchor]);
      const auto edges = next() % 4U;
      for (std::uint32_t edge = 0U; edge < edges; ++edge) {
        const auto dependency = static_cast<std::size_t>(next()) % size;
        dependencies[index].push_back(dependency);
        graph.incoming(blocks[index], graph.value(blocks[dependency]));
      }
    }
    bool changed = true;
    while (changed) {
      changed = false;
      for (std::size_t index = 0U; index < size; ++index)
        for (const auto dependency : dependencies[index]) {
          const auto before = reachable[index];
          reachable[index] |= reachable[dependency];
          changed = changed || before != reachable[index];
        }
    }
    graph.normalize();
    REQUIRE(graph.verified());
    for (std::size_t index = 0U; index < size; ++index) {
      ValueType expected = ValueType::unknown;
      bool seen = false;
      std::vector<std::size_t> extents;
      bool dynamic_rank = false;
      for (std::size_t anchor = 0U; anchor < types.size(); ++anchor) {
        if ((reachable[index] & (1U << anchor)) == 0U) continue;
        const auto value_type = mir::value_type(graph.program, types[anchor]);
        const auto shape = mir::shape(graph.program, shapes[anchor]);
        if (!seen) {
          expected = value_type;
          extents = shape->extents;
          seen = true;
        } else {
          if (expected != value_type) expected = ValueType::unknown;
          if (extents.size() != shape->extents.size()) dynamic_rank = true;
          if (!dynamic_rank)
            for (std::size_t axis = 0U; axis < extents.size(); ++axis)
              if (extents[axis] != shape->extents[axis]) extents[axis] = dynamic_extent;
        }
      }
      REQUIRE(mir::value_type(graph.program, graph.argument(blocks[index]).type) == expected);
      const auto actual = mir::shape(graph.program, graph.argument(blocks[index]).shape);
      REQUIRE(actual->dynamic_rank == dynamic_rank);
      if (!dynamic_rank) REQUIRE(actual->extents == extents);
    }
    graph.argument(blocks.front()).type =
        mir::value_type(graph.program, graph.argument(blocks.front()).type) == ValueType::real
            ? TypeId{2U}
            : TypeId{1U};
    REQUIRE(!graph.verified());
  }
}
