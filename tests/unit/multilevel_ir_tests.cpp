#include <algorithm>
#include <fstream>
#include <memory_resource>
#include <sstream>
#include <type_traits>

#include "backends/backend_conformance.hpp"
#include "backends/cpp/lir.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/cpp/lowering.hpp"
#include "backends/javascript/lir.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "backends/javascript/lowering.hpp"
#include "core/backend_registry.hpp"
#include "frontends/frontend_conformance.hpp"
#include "frontends/frontend_registry.hpp"
#include "ir/dump.hpp"
#include "ir/hir.hpp"
#include "ir/mir.hpp"
#include "ir/mir_optimization.hpp"
#include "ir/pass_manager.hpp"
#include "semantic/analyzer.hpp"
#include "source/source_manager.hpp"
#include "test_framework.hpp"

namespace {

mpf::detail::hir::LoweringResult lower_python(const std::string_view source) {
  mpf::detail::SourceManager sources;
  const auto id = sources.add(source, "pipeline.py");
  const auto& descriptor = mpf::detail::python_frontend();
  auto parsed = mpf::detail::parse_with_frontend(descriptor, sources.source(id));
  REQUIRE(parsed.diagnostics.empty());
  return descriptor.lower(std::move(parsed.ast));
}

mpf::detail::hir::LoweringResult lower_source(const mpf::SourceLanguage language,
                                              const std::string_view source,
                                              const std::string& filename) {
  mpf::detail::SourceManager sources;
  const auto id = sources.add(source, filename);
  const auto* descriptor = mpf::detail::find_frontend(language);
  REQUIRE(descriptor != nullptr);
  auto parsed = mpf::detail::parse_with_frontend(*descriptor, sources.source(id));
  REQUIRE(parsed.diagnostics.empty());
  return descriptor->lower(std::move(parsed.ast));
}

std::string read_golden(const std::string& relative) {
  std::ifstream input(std::string(MPF_TEST_SOURCE_DIR) + "/golden/" + relative);
  REQUIRE(input.good());
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

std::vector<mpf::Diagnostic> observe_hir(mpf::detail::hir::Program&) {
  return {};
}

void collect_identifiers(const mpf::detail::hir::Expression& expression,
                         std::vector<const mpf::detail::hir::Expression*>& identifiers) {
  if (!expression.valid()) return;
  if (expression.kind == mpf::detail::ExpressionKind::identifier) {
    identifiers.push_back(&expression);
  }
  for (const auto& child : expression.children) collect_identifiers(child, identifiers);
}

void collect_identifiers(const std::vector<mpf::detail::hir::Statement>& statements,
                         std::vector<const mpf::detail::hir::Expression*>& identifiers) {
  for (const auto& statement : statements) {
    collect_identifiers(statement.expression, identifiers);
    collect_identifiers(statement.secondary_expression, identifiers);
    collect_identifiers(statement.tertiary_expression, identifiers);
    collect_identifiers(statement.target_expression, identifiers);
    for (const auto& expression : statement.parameter_defaults) {
      collect_identifiers(expression, identifiers);
    }
    collect_identifiers(statement.body, identifiers);
    collect_identifiers(statement.alternative, identifiers);
  }
}

void collect_binary_operations(const mpf::detail::hir::Expression& expression,
                               std::vector<mpf::detail::BinaryOperator>& operations) {
  if (!expression.valid()) return;
  if (expression.kind == mpf::detail::ExpressionKind::binary) {
    operations.push_back(expression.operation);
  }
  for (const auto& child : expression.children) collect_binary_operations(child, operations);
}

void collect_binary_operations(const std::vector<mpf::detail::hir::Statement>& statements,
                               std::vector<mpf::detail::BinaryOperator>& operations) {
  for (const auto& statement : statements) {
    collect_binary_operations(statement.expression, operations);
    collect_binary_operations(statement.secondary_expression, operations);
    collect_binary_operations(statement.tertiary_expression, operations);
    collect_binary_operations(statement.target_expression, operations);
    collect_binary_operations(statement.body, operations);
    collect_binary_operations(statement.alternative, operations);
  }
}

}  // namespace

TEST_CASE("frontends lower language-owned AST artifacts into verified HIR") {
  auto lowered = lower_python("value = 40 + 2\nprint(value)\n");
  REQUIRE(lowered.diagnostics.empty());
  REQUIRE(lowered.program.language == mpf::SourceLanguage::python);
  REQUIRE(lowered.program.node_count > lowered.program.statements.size());
  REQUIRE(lowered.program.statements.front().id.valid());
  REQUIRE(lowered.program.semantics.truthiness == mpf::detail::semantic::Truthiness::dynamic);
  REQUIRE(mpf::detail::hir::verify(lowered.program, "test").empty());

  mpf::detail::SourceManager sources;
  const auto source_id = sources.add("value = 1\n", "inventory.py");
  const auto& frontend = mpf::detail::python_frontend();
  auto parsed = mpf::detail::parse_with_frontend(frontend, sources.source(source_id));
  REQUIRE(parsed.diagnostics.empty());
  const auto* ast = std::get_if<mpf::detail::python::ast::Program>(&parsed.ast);
  REQUIRE(ast != nullptr);
  REQUIRE(ast->node_count() + 1 == ast->records.size());
  REQUIRE(ast->records[1].index == 0);
  REQUIRE(ast->roots.front().valid());
  REQUIRE(!mpf::detail::dump_frontend_ast(parsed.ast).empty());
  REQUIRE(
      (!std::is_same_v<mpf::detail::python::ast::Statement, mpf::detail::matlab::ast::Statement>));
  REQUIRE(
      (!std::is_same_v<mpf::detail::matlab::ast::Statement, mpf::detail::fortran::ast::Statement>));
  REQUIRE((!std::is_same_v<mpf::detail::fortran::ast::Statement,
                           mpf::detail::typescript::ast::Statement>));
  REQUIRE(frontend.verify(parsed.ast).empty());
}

TEST_CASE("statement parsers construct language arenas directly and recover without orphan nodes") {
  static_assert(!std::is_same_v<mpf::detail::python::ast::ParseResult,
                                mpf::detail::matlab::ast::ParseResult>);
  static_assert(!std::is_same_v<mpf::detail::matlab::ast::ParseResult,
                                mpf::detail::fortran::ast::ParseResult>);
  static_assert(!std::is_same_v<mpf::detail::fortran::ast::ParseResult,
                                mpf::detail::typescript::ast::ParseResult>);

  std::pmr::monotonic_buffer_resource arena;
  mpf::detail::FrontendParseOptions options;
  options.memory_resource = &arena;
  const struct Case {
    const mpf::detail::FrontendDescriptor* frontend;
    const char* source;
    const char* filename;
  } cases[]{{&mpf::detail::python_frontend(), "else:\n    print(1)\nprint(2)\n", "recover.py"},
            {&mpf::detail::matlab_frontend(), "else\ndisp(1)\nend\ndisp(2)\n", "recover.m"},
            {&mpf::detail::fortran_frontend(),
             "program recover\nelse\nprint *, 1\nend if\nprint *, 2\nend program recover\n",
             "recover.f90"},
            {&mpf::detail::typescript_frontend(),
             "else { console.log(1); }\nconst answer: number = 42;\n", "recover.ts"}};

  for (const auto& test : cases) {
    mpf::detail::SourceManager sources;
    const auto source = sources.add(test.source, test.filename);
    auto parsed = mpf::detail::parse_with_frontend(*test.frontend, sources.source(source), options);
    REQUIRE(!parsed.diagnostics.empty());
    REQUIRE(mpf::detail::frontend_ast_node_count(parsed.ast) != 0U);
    REQUIRE(test.frontend->verify(parsed.ast).empty());
    const auto owns_arena = std::visit(
        [&](const auto& program) {
          using Program = std::decay_t<decltype(program)>;
          if constexpr (std::is_same_v<Program, std::monostate>) {
            return false;
          } else {
            return program.resource() == &arena;
          }
        },
        parsed.ast);
    REQUIRE(owns_arena);
  }
}

TEST_CASE("Matlab binary operator identity remains typed through HIR and MIR") {
  auto lowered = lower_source(mpf::SourceLanguage::matlab,
                              "left = [1 2; 3 4];\nright = left .* 2;\nvalue = left * right;\n",
                              "operators.m");
  std::vector<mpf::detail::BinaryOperator> hir_operations;
  collect_binary_operations(lowered.program.statements, hir_operations);
  REQUIRE(std::find(hir_operations.begin(), hir_operations.end(),
                    mpf::detail::BinaryOperator::elementwise_multiply) != hir_operations.end());
  REQUIRE(std::find(hir_operations.begin(), hir_operations.end(),
                    mpf::detail::BinaryOperator::multiply) != hir_operations.end());

  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  std::vector<mpf::detail::BinaryOperator> mir_operations;
  for (std::size_t index = 1; index < mir.program.attributes.expressions.size(); ++index) {
    const auto operation = mir.program.attributes.expressions[index].operation;
    if (operation != mpf::detail::BinaryOperator::none) mir_operations.push_back(operation);
  }
  REQUIRE(std::find(mir_operations.begin(), mir_operations.end(),
                    mpf::detail::BinaryOperator::elementwise_multiply) != mir_operations.end());
  REQUIRE(std::find(mir_operations.begin(), mir_operations.end(),
                    mpf::detail::BinaryOperator::multiply) != mir_operations.end());

  auto missing_operation = mir.program;
  const auto found = std::find_if(
      missing_operation.attributes.expressions.begin() + 1,
      missing_operation.attributes.expressions.end(), [](const auto& attributes) {
        return attributes.operation == mpf::detail::BinaryOperator::elementwise_multiply;
      });
  REQUIRE(found != missing_operation.attributes.expressions.end());
  found->operation = mpf::detail::BinaryOperator::none;
  REQUIRE(!mpf::detail::mir::verify(missing_operation, "missing-binary-operator").empty());
}

TEST_CASE("HIR pass manager verifies revisions and records instrumentation") {
  auto lowered = lower_python("value = 1\n");
  mpf::detail::PassManager<mpf::detail::hir::Program> passes(&mpf::detail::hir::verify);
  passes.add({"observation", &observe_hir, false});
  const auto diagnostics = passes.run(lowered.program);
  REQUIRE(diagnostics.empty());
  REQUIRE(lowered.program.revision == 0);
  REQUIRE(passes.instrumentation().size() == 1);
  REQUIRE(passes.instrumentation().front().name == "observation");
}

TEST_CASE("equivalent Python and Matlab sources share normalized HIR golden") {
  auto python =
      lower_source(mpf::SourceLanguage::python, "value = 1\nprint(value)\n", "normalized.py");
  auto matlab =
      lower_source(mpf::SourceLanguage::matlab, "value = 1;\ndisp(value);\n", "normalized.m");
  REQUIRE(python.diagnostics.empty());
  REQUIRE(matlab.diagnostics.empty());
  const auto expected = read_golden("hir/scalar_assignment.hir");
  REQUIRE(mpf::detail::dump_normalized_hir(python.program) == expected);
  REQUIRE(mpf::detail::dump_normalized_hir(matlab.program) == expected);
}

TEST_CASE("analysis manager caches by revision and honors precise preservation") {
  auto lowered = lower_python("value = 1\n");
  mpf::detail::AnalysisManager<mpf::detail::hir::Program> analyses;
  std::size_t computations = 0;
  const auto compute = [&](const mpf::detail::hir::Program& program) {
    ++computations;
    return program.node_count;
  };
  REQUIRE(analyses.get<std::size_t>(lowered.program, "node-count", compute) ==
          lowered.program.node_count);
  REQUIRE(analyses.get<std::size_t>(lowered.program, "node-count", compute) ==
          lowered.program.node_count);
  REQUIRE(computations == 1);

  ++lowered.program.revision;
  analyses.invalidate(lowered.program.revision, {"node-count"});
  REQUIRE(analyses.get<std::size_t>(lowered.program, "node-count", compute) ==
          lowered.program.node_count);
  REQUIRE(computations == 1);

  ++lowered.program.revision;
  analyses.invalidate(lowered.program.revision, {});
  REQUIRE(analyses.get<std::size_t>(lowered.program, "node-count", compute) ==
          lowered.program.node_count);
  REQUIRE(computations == 2);
}

TEST_CASE("HIR and MIR dumps are deterministic and stage specific") {
  auto lowered = lower_python("value = [1, 2]\nprint(value[0])\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  const auto first_hir = mpf::detail::dump_hir(lowered.program);
  const auto second_hir = mpf::detail::dump_hir(lowered.program);
  REQUIRE(first_hir == second_hir);
  REQUIRE(first_hir.find("hir-v1") != std::string::npos);
  REQUIRE(first_hir.find("stmt %h") != std::string::npos);
  const auto first_semantics = mpf::detail::dump_semantics(analysis.semantics);
  REQUIRE(first_semantics == mpf::detail::dump_semantics(analysis.semantics));
  REQUIRE(first_semantics.find("semantic-v2") != std::string::npos);

  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  const auto alias_effects = mpf::detail::mir::analyze_alias_effects(mir.program);
  const auto first_mir = mpf::detail::dump_mir(mir.program, alias_effects);
  REQUIRE(first_mir == mpf::detail::dump_mir(mir.program, alias_effects));
  REQUIRE(first_mir.find("mir-v6") != std::string::npos);
  REQUIRE(first_mir.find("alias-effect-v3") != std::string::npos);
  REQUIRE(first_mir.find("memory-accesses=[") != std::string::npos);
  REQUIRE(first_mir.find("function @f") != std::string::npos);
  REQUIRE(first_mir.find("terminator op") != std::string::npos);
  REQUIRE(first_mir.find("expression %mexpr") != std::string::npos);
  REQUIRE(first_mir.find("operation %mstmt") != std::string::npos);
}

TEST_CASE("MIR alias and effect analysis is independent revision-bound and cacheable") {
  auto lowered = lower_python(
      "base = 40\n"
      "def bump(value):\n"
      "    local = value + 1\n"
      "    return base + local\n"
      "result = bump(1)\n"
      "print(result)\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());

  mpf::detail::AnalysisManager<mpf::detail::mir::Program> analyses;
  std::size_t computations = 0;
  const auto compute = [&](const mpf::detail::mir::Program& program) {
    ++computations;
    return mpf::detail::mir::analyze_alias_effects(program);
  };
  const auto& first =
      analyses.get<mpf::detail::mir::AliasEffectTable>(mir.program, "alias-effect", compute);
  const auto& second =
      analyses.get<mpf::detail::mir::AliasEffectTable>(mir.program, "alias-effect", compute);
  REQUIRE(&first == &second);
  REQUIRE(computations == 1U);
  REQUIRE(mpf::detail::mir::verify_alias_effects(mir.program, first, "cache").empty());
  const auto& independent = analyses.get<std::size_t>(
      mir.program, "independent-analysis",
      [](const mpf::detail::mir::Program& program) { return program.instructions.size(); });
  REQUIRE(independent == mir.program.instructions.size());
  REQUIRE(first.mir_revision == mir.program.revision);
  REQUIRE(&first ==
          &analyses.get<mpf::detail::mir::AliasEffectTable>(mir.program, "alias-effect", compute));
  REQUIRE(computations == 1U);
  const auto global = std::find_if(
      mir.program.storages.begin() + 1, mir.program.storages.end(),
      [](const mpf::detail::mir::StorageData& storage) { return storage.name == "base"; });
  REQUIRE(global != mir.program.storages.end());
  REQUIRE(global->kind == mpf::detail::mir::StorageKind::global);
  REQUIRE(global->lifetime == mpf::detail::mir::StorageLifetime::module);
  REQUIRE(std::count_if(mir.program.storages.begin() + 1, mir.program.storages.end(),
                        [](const mpf::detail::mir::StorageData& storage) {
                          return storage.name == "base";
                        }) == 1);
  const auto bump = std::find_if(
      mir.program.functions.begin() + 1, mir.program.functions.end(),
      [](const mpf::detail::mir::Function& function) { return function.name == "bump"; });
  REQUIRE(bump != mir.program.functions.end());
  const auto* bump_effects = first.function(bump->id);
  REQUIRE(bump_effects != nullptr);
  REQUIRE(bump_effects->parameter_reads.front());
  REQUIRE(!bump_effects->parameter_writes.front());
  REQUIRE(mpf::detail::mir::has_effect(bump_effects->effects, mpf::detail::mir::Effect::read));
  REQUIRE(!mpf::detail::mir::has_effect(bump_effects->effects, mpf::detail::mir::Effect::write));
  REQUIRE(first.calls.size() == 1U);
  REQUIRE(first.calls.front().writes.empty());

  auto stale = first;
  ++mir.program.revision;
  REQUIRE(!mpf::detail::mir::verify_alias_effects(mir.program, stale, "stale").empty());
  const auto& refreshed =
      analyses.get<mpf::detail::mir::AliasEffectTable>(mir.program, "alias-effect", compute);
  REQUIRE(computations == 2U);
  REQUIRE(refreshed.mir_revision == mir.program.revision);

  auto weakened = refreshed;
  const auto write = std::find_if(
      weakened.instructions.begin() + 1, weakened.instructions.end(),
      [](const mpf::detail::mir::InstructionEffectFacts& facts) { return !facts.writes.empty(); });
  REQUIRE(write != weakened.instructions.end());
  write->effects = mpf::detail::mir::Effect::none;
  REQUIRE(!mpf::detail::mir::verify_alias_effects(mir.program, weakened, "weakened").empty());
}

TEST_CASE("MIR memory dependence analysis is CFG-aware revision-bound and cacheable") {
  auto lowered = lower_python(
      "value = 0\n"
      "if True:\n"
      "    value = 1\n"
      "else:\n"
      "    value = 2\n"
      "print(value)\n"
      "while value < 4:\n"
      "    value = value + 1\n"
      "print(value)\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  const auto alias_effects = mpf::detail::mir::analyze_alias_effects(mir.program);
  REQUIRE(mpf::detail::mir::verify_alias_effects(mir.program, alias_effects, "dependence-input")
              .empty());

  mpf::detail::AnalysisManager<mpf::detail::mir::Program> analyses;
  std::size_t computations = 0;
  const auto compute = [&](const mpf::detail::mir::Program& program) {
    ++computations;
    return mpf::detail::mir::analyze_memory_dependences(program, alias_effects);
  };
  const auto& dependences = analyses.get<mpf::detail::mir::MemoryDependenceTable>(
      mir.program, "memory-dependence", compute);
  const auto& cached = analyses.get<mpf::detail::mir::MemoryDependenceTable>(
      mir.program, "memory-dependence", compute);
  REQUIRE(&dependences == &cached);
  REQUIRE(computations == 1U);
  REQUIRE(dependences.complete);
  REQUIRE(dependences.instructions.size() == mir.program.instructions.size());
  REQUIRE(dependences.dependence_count + 1U == dependences.dependences.size());
  REQUIRE(mpf::detail::mir::verify_memory_dependences(mir.program, alias_effects, dependences,
                                                      "cfg-memory-dependence")
              .empty());
  REQUIRE(std::any_of(dependences.dependences.begin() + 1, dependences.dependences.end(),
                      [](const mpf::detail::mir::MemoryDependence& dependence) {
                        return dependence.kind == mpf::detail::mir::MemoryDependenceKind::flow;
                      }));
  REQUIRE(std::any_of(dependences.dependences.begin() + 1, dependences.dependences.end(),
                      [](const mpf::detail::mir::MemoryDependence& dependence) {
                        return dependence.kind == mpf::detail::mir::MemoryDependenceKind::anti;
                      }));
  REQUIRE(std::any_of(dependences.dependences.begin() + 1, dependences.dependences.end(),
                      [](const mpf::detail::mir::MemoryDependence& dependence) {
                        return dependence.kind == mpf::detail::mir::MemoryDependenceKind::output;
                      }));
  REQUIRE(std::any_of(dependences.dependences.begin() + 1, dependences.dependences.end(),
                      [](const mpf::detail::mir::MemoryDependence& dependence) {
                        return dependence.loop_carried;
                      }));
  REQUIRE(std::any_of(
      dependences.instructions.begin() + 1, dependences.instructions.end(),
      [&](const mpf::detail::mir::InstructionMemoryDependenceFacts& facts) {
        return std::count_if(facts.incoming.begin(), facts.incoming.end(), [&](const auto id) {
                 const auto* dependence = dependences.dependence(id);
                 return dependence != nullptr &&
                        dependence->kind == mpf::detail::mir::MemoryDependenceKind::flow;
               }) >= 2;
      }));
  const auto dumped = mpf::detail::dump_mir(mir.program, alias_effects, dependences);
  REQUIRE(dumped.find("memory-dependence-v1") != std::string::npos);
  REQUIRE(dumped.find("loop-carried=1") != std::string::npos);

  auto corrupted = dependences;
  corrupted.dependences[1].relation = mpf::detail::mir::AliasClass::no_alias;
  REQUIRE(!mpf::detail::mir::verify_memory_dependences(mir.program, alias_effects, corrupted,
                                                       "corrupt-memory-dependence")
               .empty());
  auto corrupt_sentinel = dependences;
  corrupt_sentinel.instructions.front().origin = mpf::detail::InstructionId{1};
  REQUIRE(!mpf::detail::mir::verify_memory_dependences(mir.program, alias_effects, corrupt_sentinel,
                                                       "corrupt-memory-sentinel")
               .empty());
}

TEST_CASE("MIR memory dependences refine disjoint regions and preserve unknown barriers") {
  auto lowered = lower_python(
      "values = [1, 2, 3, 4]\n"
      "first = values[0]\n"
      "values[1] = 7\n"
      "print(first)\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  auto alias_effects = mpf::detail::mir::analyze_alias_effects(mir.program);
  const auto dependences = mpf::detail::mir::analyze_memory_dependences(mir.program, alias_effects);
  REQUIRE(mpf::detail::mir::verify_memory_dependences(mir.program, alias_effects, dependences,
                                                      "regional-memory-dependence")
              .empty());
  const auto read =
      std::find_if(mir.program.instructions.begin() + 1, mir.program.instructions.end(),
                   [](const mpf::detail::mir::Instruction& instruction) {
                     return instruction.opcode == mpf::detail::mir::Opcode::index;
                   });
  const auto write =
      std::find_if(mir.program.instructions.begin() + 1, mir.program.instructions.end(),
                   [](const mpf::detail::mir::Instruction& instruction) {
                     return instruction.opcode == mpf::detail::mir::Opcode::store_indexed;
                   });
  REQUIRE(read != mir.program.instructions.end());
  REQUIRE(write != mir.program.instructions.end());
  REQUIRE(std::none_of(dependences.dependences.begin() + 1, dependences.dependences.end(),
                       [&](const mpf::detail::mir::MemoryDependence& dependence) {
                         return dependence.source.instruction == read->id &&
                                dependence.target.instruction == write->id;
                       }));

  auto external_program = mir.program;
  const auto external =
      std::find_if(external_program.instructions.begin() + 1, external_program.instructions.end(),
                   [](const mpf::detail::mir::Instruction& instruction) {
                     return instruction.opcode == mpf::detail::mir::Opcode::output;
                   });
  REQUIRE(external != external_program.instructions.end());
  external->opcode = mpf::detail::mir::Opcode::call;
  external->callee = {};
  external->intrinsic = mpf::detail::IntrinsicId::none;
  alias_effects = mpf::detail::mir::analyze_alias_effects(external_program);
  REQUIRE(mpf::detail::mir::verify_alias_effects(external_program, alias_effects,
                                                 "unknown-barrier-input")
              .empty());
  const auto barriers =
      mpf::detail::mir::analyze_memory_dependences(external_program, alias_effects);
  REQUIRE(mpf::detail::mir::verify_memory_dependences(external_program, alias_effects, barriers,
                                                      "unknown-barrier")
              .empty());
  REQUIRE(std::any_of(barriers.dependences.begin() + 1, barriers.dependences.end(),
                      [&](const mpf::detail::mir::MemoryDependence& dependence) {
                        return dependence.target.instruction == external->id &&
                               dependence.barrier && dependence.target.unknown &&
                               dependence.relation == mpf::detail::mir::AliasClass::may_alias;
                      }));
}

TEST_CASE("MIR memory dependences conservatively cut irreducible CFG cycles") {
  mpf::detail::mir::Program program;
  program.storages.resize(2);
  program.storages[1].name = "shared";
  program.storages[1].kind = mpf::detail::mir::StorageKind::local;
  program.storages[1].lifetime = mpf::detail::mir::StorageLifetime::function;
  program.instructions.resize(3);
  program.instructions[1].id = mpf::detail::InstructionId{1};
  program.instructions[1].opcode = mpf::detail::mir::Opcode::store;
  program.instructions[1].location = {2, 1};
  program.instructions[2].id = mpf::detail::InstructionId{2};
  program.instructions[2].opcode = mpf::detail::mir::Opcode::load;
  program.instructions[2].location = {3, 1};
  program.attributes.instruction_count = 2;
  program.attributes.instructions.resize(3);
  program.attributes.instructions[1].origin = mpf::detail::InstructionId{1};
  program.attributes.instructions[1].memory_accesses.push_back(
      {mpf::detail::StorageId{1},
       mpf::detail::StorageId{1},
       {},
       mpf::detail::mir::MemoryAccessMode::write});
  program.attributes.instructions[2].origin = mpf::detail::InstructionId{2};
  program.attributes.instructions[2].memory_accesses.push_back(
      {mpf::detail::StorageId{1},
       mpf::detail::StorageId{1},
       {},
       mpf::detail::mir::MemoryAccessMode::read});
  program.blocks.resize(4);
  for (std::size_t index = 1; index < program.blocks.size(); ++index) {
    program.blocks[index].id =
        mpf::detail::BlockId{static_cast<mpf::detail::BlockId::value_type>(index)};
  }
  program.blocks[1].terminator.kind = mpf::detail::mir::TerminatorKind::conditional_branch;
  program.blocks[1].terminator.successors = {mpf::detail::BlockId{2}, mpf::detail::BlockId{3}};
  program.blocks[2].instructions = {mpf::detail::InstructionId{1}};
  program.blocks[2].terminator.kind = mpf::detail::mir::TerminatorKind::branch;
  program.blocks[2].terminator.successors = {mpf::detail::BlockId{3}};
  program.blocks[3].instructions = {mpf::detail::InstructionId{2}};
  program.blocks[3].terminator.kind = mpf::detail::mir::TerminatorKind::branch;
  program.blocks[3].terminator.successors = {mpf::detail::BlockId{2}};
  program.functions.resize(2);
  program.functions[1].id = mpf::detail::MirFunctionId{1};
  program.functions[1].entry = mpf::detail::BlockId{1};
  program.functions[1].blocks = {mpf::detail::BlockId{1}, mpf::detail::BlockId{2},
                                 mpf::detail::BlockId{3}};

  const auto alias_effects = mpf::detail::mir::analyze_alias_effects(program);
  REQUIRE(mpf::detail::mir::verify_alias_effects(program, alias_effects, "irreducible-alias-input")
              .empty());
  const auto dependences = mpf::detail::mir::analyze_memory_dependences(program, alias_effects);
  REQUIRE(mpf::detail::mir::verify_memory_dependences(program, alias_effects, dependences,
                                                      "irreducible-memory-dependence")
              .empty());
  REQUIRE(std::any_of(dependences.dependences.begin() + 1, dependences.dependences.end(),
                      [](const mpf::detail::mir::MemoryDependence& dependence) {
                        return dependence.loop_carried &&
                               dependence.kind == mpf::detail::mir::MemoryDependenceKind::output &&
                               dependence.source.instruction == dependence.target.instruction;
                      }));
}

TEST_CASE("default MIR optimization is verified deterministic and analysis-safe") {
  auto lowered = lower_python("value = (1 + 2) * (5 - 2)\nprint(value)\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());

  const auto root = std::find_if(
      mir.program.expressions.begin() + 1, mir.program.expressions.end(),
      [&](const mpf::detail::mir::Expression& expression) {
        const auto* attributes = mpf::detail::mir::attributes(mir.program, expression.id);
        return expression.valid() && expression.kind == mpf::detail::ExpressionKind::binary &&
               attributes != nullptr && attributes->spelling == "*";
      });
  REQUIRE(root != mir.program.expressions.end());
  const auto duplicate_shape = mpf::detail::ShapeId{
      static_cast<mpf::detail::ShapeId::value_type>(mir.program.shapes.size())};
  mir.program.shapes.push_back(mir.program.shapes[root->shape_id.value()]);
  root->shape_id = duplicate_shape;
  mir.program.instructions[root->instruction.value()].shape = duplicate_shape;
  REQUIRE(mpf::detail::mir::verify(mir.program, "optimizer-input").empty());

  const auto revision = mir.program.revision;
  const auto instructions = mir.program.instructions.size() - 1U;
  const auto shapes = mir.program.shapes.size();
  const auto optimized = mpf::detail::mir::run_default_optimization_pipeline(mir.program);
  REQUIRE(optimized.diagnostics.empty());
  REQUIRE(optimized.instrumentation.size() == 4U);
  REQUIRE(optimized.instrumentation[0].name == "mir-shape-canonicalization");
  REQUIRE(optimized.instrumentation[1].name == "mir-copy-propagation");
  REQUIRE(optimized.instrumentation[2].name == "mir-constant-folding-dce");
  REQUIRE(optimized.instrumentation[3].name == "mir-cfg-cleanup");
  REQUIRE(mir.program.revision == revision + 4U);
  REQUIRE(mir.program.attributes.mir_revision == mir.program.revision);
  REQUIRE(mir.program.attributes.instruction_count + 1U == mir.program.instructions.size());
  REQUIRE(mir.program.attributes.instructions.size() == mir.program.instructions.size());
  for (std::size_t index = 1; index < mir.program.attributes.instructions.size(); ++index) {
    REQUIRE(mir.program.attributes.instructions[index].origin.value() == index);
  }
  REQUIRE(optimized.statistics.folded_expressions == 3U);
  REQUIRE(optimized.statistics.retired_expressions != 0U);
  REQUIRE(optimized.statistics.removed_instructions != 0U);
  REQUIRE(optimized.statistics.canonicalized_shapes == 1U);
  REQUIRE(optimized.statistics.instructions_before == instructions);
  REQUIRE(optimized.statistics.instructions_after < optimized.statistics.instructions_before);
  REQUIRE(mir.program.shapes.size() + 1U == shapes);
  REQUIRE(mpf::detail::mir::verify(mir.program, "optimizer-output").empty());

  auto stale_tombstone = mir.program;
  const auto retired = std::find_if(
      stale_tombstone.expressions.begin() + 1, stale_tombstone.expressions.end(),
      [](const mpf::detail::mir::Expression& expression) { return expression.retired; });
  REQUIRE(retired != stale_tombstone.expressions.end());
  stale_tombstone.attributes.expressions[retired->id.value()].spelling = "stale";
  REQUIRE(!mpf::detail::mir::verify(stale_tombstone, "stale-tombstone").empty());

  const auto folded = std::find_if(
      mir.program.expressions.begin() + 1, mir.program.expressions.end(),
      [&](const mpf::detail::mir::Expression& expression) {
        const auto* attributes = mpf::detail::mir::attributes(mir.program, expression.id);
        return expression.valid() &&
               expression.kind == mpf::detail::ExpressionKind::number_literal &&
               attributes != nullptr && attributes->spelling == "9";
      });
  REQUIRE(folded != mir.program.expressions.end());
  const auto effects = mpf::detail::mir::analyze_alias_effects(mir.program);
  REQUIRE(mpf::detail::mir::verify_alias_effects(mir.program, effects, "optimized").empty());

  const auto second = mpf::detail::mir::run_default_optimization_pipeline(mir.program);
  REQUIRE(second.diagnostics.empty());
  REQUIRE(second.statistics.folded_expressions == 0U);
  REQUIRE(second.statistics.retired_expressions == 0U);
  REQUIRE(second.statistics.removed_instructions == 0U);
  REQUIRE(second.statistics.propagated_block_arguments == 0U);
  REQUIRE(second.statistics.removed_blocks == 0U);
  REQUIRE(second.statistics.canonicalized_shapes == 0U);
  REQUIRE(second.statistics.instructions_before == second.statistics.instructions_after);
  REQUIRE(second.statistics.blocks_before == second.statistics.blocks_after);
}

TEST_CASE("MIR constant folding rejects overflow and target precision loss") {
  for (const auto source : {"value = 9223372036854775807 + 1\nprint(value)\n",
                            "value = 9007199254740992 + 1\nprint(value)\n"}) {
    auto lowered = lower_python(source);
    auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
    REQUIRE(analysis.empty());
    auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                                std::move(analysis.semantics), analysis.names);
    REQUIRE(mir.diagnostics.empty());
    const auto optimized = mpf::detail::mir::run_default_optimization_pipeline(mir.program);
    REQUIRE(optimized.diagnostics.empty());
    REQUIRE(optimized.statistics.folded_expressions == 0U);
    REQUIRE(optimized.statistics.retired_expressions == 0U);
    REQUIRE(mpf::detail::mir::verify(mir.program, "precision-output").empty());
  }
}

TEST_CASE("frontends seed complete revision-checked dense semantic side tables") {
  auto lowered = lower_python("values = [[1, 2], [3, 4]]\nprint(values[1][0])\n");
  const auto nodes = lowered.program.node_count;
  const auto hir_seed_before = mpf::detail::dump_semantics(lowered.semantics);
  auto stale_program = lowered.program;
  auto stale_seed = lowered.semantics;
  ++stale_seed.hir_revision;
  auto rejected = mpf::detail::analyze_program(stale_program, std::move(stale_seed));
  REQUIRE(!rejected.empty());
  auto surplus_program = lowered.program;
  auto surplus_seed = lowered.semantics;
  surplus_seed.expressions.push_back({});
  auto surplus = mpf::detail::analyze_program(surplus_program, std::move(surplus_seed));
  REQUIRE(!surplus.empty());
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  REQUIRE(analysis.semantics.hir_node_count == nodes);
  REQUIRE(analysis.semantics.nodes.size() == nodes + 1U);
  REQUIRE(analysis.semantics.hir_revision == lowered.program.revision);
  REQUIRE(analysis.semantics.nodes.front().kind == mpf::detail::hir::SemanticNodeKind::absent);
  for (std::size_t id = 1; id <= nodes; ++id) {
    REQUIRE(analysis.semantics.nodes[id].kind != mpf::detail::hir::SemanticNodeKind::absent);
  }
  const auto assignment_id = lowered.program.statements.front().id;
  const auto* assignment = analysis.semantics.statement(assignment_id);
  REQUIRE(assignment != nullptr);
  REQUIRE(assignment->declared_type == mpf::detail::ValueType::list);
  REQUIRE((assignment->shape == std::vector<std::size_t>{2, 2}));
  REQUIRE(mpf::detail::dump_semantics(analysis.semantics) != hir_seed_before);
  REQUIRE(mpf::detail::hir::verify_semantics(lowered.program, analysis.semantics, "test").empty());

  auto stale = analysis.semantics;
  auto stale_names = analysis.names;
  ++lowered.program.revision;
  REQUIRE(!mpf::detail::hir::verify_semantics(lowered.program, stale, "stale").empty());
  auto failed =
      mpf::detail::mir::lower_from_hir(std::move(lowered.program), std::move(stale), stale_names);
  REQUIRE(!failed.diagnostics.empty());
  REQUIRE(failed.program.instructions.size() == 1U);

  auto unpacked = lower_python("left, right = (20, 22)\nprint(left + right)\n");
  auto unpacked_analysis =
      mpf::detail::analyze_program(unpacked.program, std::move(unpacked.semantics));
  REQUIRE(unpacked_analysis.empty());
  const auto unpack_id = unpacked.program.statements.front().id;
  const auto* unpack_facts = unpacked_analysis.semantics.statement(unpack_id);
  REQUIRE(unpack_facts != nullptr);
  REQUIRE(unpack_facts->target_pattern.valid());
  REQUIRE(unpack_facts->target_pattern.children.size() == 2U);
  REQUIRE(!unpack_facts->target_pattern.children.front().access_path.empty());
}

TEST_CASE("argument normalization revises HIR and keeps analysis tables dense") {
  const std::string source =
      "def combine(left, right=2):\n"
      "    return left + right\n"
      "print(combine(40))\n";
  auto lowered = lower_python(source);
  const auto initial_nodes = lowered.program.node_count;
  const auto initial_revision = lowered.program.revision;
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  REQUIRE(lowered.program.revision == initial_revision + 1U);
  REQUIRE(lowered.program.node_count > initial_nodes);
  REQUIRE(analysis.semantics.hir_node_count == lowered.program.node_count);
  REQUIRE(analysis.semantics.nodes.size() == lowered.program.node_count + 1U);
  REQUIRE(analysis.flow.hir_node_count == lowered.program.node_count);
  REQUIRE(analysis.flow.nodes.size() == lowered.program.node_count + 1U);
  REQUIRE(analysis.names.hir_node_count == lowered.program.node_count);
  REQUIRE(analysis.names.nodes.size() == lowered.program.node_count + 1U);
  REQUIRE(mpf::detail::hir::verify(lowered.program, "normalized-call").empty());
  REQUIRE(mpf::detail::hir::verify_semantics(lowered.program, analysis.semantics, "normalized-call")
              .empty());
  REQUIRE(mpf::detail::verify_flow(lowered.program, analysis.flow, "normalized-call").empty());
  REQUIRE(mpf::detail::verify_names(lowered.program, analysis.names, "normalized-call").empty());

  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::python;
  options.resource_limits.max_hir_nodes = initial_nodes;
  const auto limited = mpf::Transpiler{}.transpile(source, options);
  REQUIRE(!limited.success());
  REQUIRE(std::any_of(limited.diagnostics.begin(), limited.diagnostics.end(),
                      [](const mpf::Diagnostic& diagnostic) {
                        return diagnostic.code == "MPF0010" &&
                               diagnostic.message.find("hir-nodes") != std::string::npos;
                      }));
}

TEST_CASE("control flow analysis is independent revision-bound and dense") {
  auto lowered = lower_python(
      "def choose(flag):\n"
      "    if flag:\n"
      "        return 1\n"
      "    else:\n"
      "        return 2\n"
      "    print(3)\n"
      "print(choose(True))\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(std::any_of(analysis.diagnostics.begin(), analysis.diagnostics.end(),
                      [](const mpf::Diagnostic& diagnostic) {
                        return diagnostic.severity == mpf::DiagnosticSeverity::warning &&
                               diagnostic.code == "MPF2101";
                      }));
  REQUIRE(mpf::detail::verify_flow(lowered.program, analysis.flow, "test").empty());

  const auto& function = lowered.program.statements.front();
  const auto& conditional = function.body.front();
  const auto& unreachable = function.body.back();
  const auto* function_flow = analysis.flow.statement(function.id);
  const auto* conditional_flow = analysis.flow.statement(conditional.id);
  const auto* unreachable_flow = analysis.flow.statement(unreachable.id);
  REQUIRE(function_flow != nullptr);
  REQUIRE(conditional_flow != nullptr);
  REQUIRE(unreachable_flow != nullptr);
  REQUIRE(function_flow->body_terminates);
  REQUIRE(conditional_flow->terminates);
  REQUIRE(conditional_flow->body_terminates);
  REQUIRE(conditional_flow->alternative_terminates);
  REQUIRE(!unreachable_flow->reachable);
  REQUIRE(unreachable_flow->function_depth == 1U);

  auto stale = analysis.flow;
  ++lowered.program.revision;
  REQUIRE(!mpf::detail::verify_flow(lowered.program, stale, "stale").empty());
}

TEST_CASE("name analysis owns dense lexical scopes symbols and shadowing") {
  auto lowered = lower_python(
      "value = 40\n"
      "def compute(value):\n"
      "    local = value + 2\n"
      "    return local\n"
      "print(compute(value) + abs(-1))\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  REQUIRE(analysis.names.hir_revision == lowered.program.revision);
  REQUIRE(analysis.names.hir_node_count == lowered.program.node_count);
  REQUIRE(analysis.names.nodes.size() == lowered.program.node_count + 1U);
  REQUIRE(analysis.names.global_scope.valid());
  REQUIRE(analysis.names.scopes.size() == 3U);
  REQUIRE(mpf::detail::verify_names(lowered.program, analysis.names, "test").empty());

  const auto& global_assignment = lowered.program.statements[0];
  const auto& function = lowered.program.statements[1];
  const auto* global_definition =
      analysis.names.use(global_assignment.id, mpf::detail::NameRole::assignment);
  const auto* function_definition =
      analysis.names.use(function.id, mpf::detail::NameRole::declaration);
  const auto* parameter_definition =
      analysis.names.use(function.id, mpf::detail::NameRole::parameter);
  REQUIRE(global_definition != nullptr);
  REQUIRE(function_definition != nullptr);
  REQUIRE(parameter_definition != nullptr);
  REQUIRE(global_definition->symbol != parameter_definition->symbol);
  REQUIRE(function_definition->binding == mpf::detail::BindingKind::function);
  const auto function_scope = analysis.names.function_scope(function.id);
  REQUIRE(function_scope.valid());
  REQUIRE(analysis.names.scope(function_scope)->parent == analysis.names.global_scope);
  REQUIRE(analysis.names.symbol(parameter_definition->symbol)->scope == function_scope);

  std::vector<const mpf::detail::hir::Expression*> identifiers;
  collect_identifiers(lowered.program.statements, identifiers);
  const auto find_identifier = [&](const std::string_view name, const std::size_t line) {
    return std::find_if(identifiers.begin(), identifiers.end(), [&](const auto* expression) {
      return expression->value == name && expression->location.line == line;
    });
  };
  const auto parameter_reference = find_identifier("value", 3);
  const auto local_reference = find_identifier("local", 4);
  const auto global_reference = find_identifier("value", 5);
  const auto function_reference = find_identifier("compute", 5);
  const auto builtin_reference = find_identifier("abs", 5);
  REQUIRE(parameter_reference != identifiers.end());
  REQUIRE(local_reference != identifiers.end());
  REQUIRE(global_reference != identifiers.end());
  REQUIRE(function_reference != identifiers.end());
  REQUIRE(builtin_reference != identifiers.end());
  REQUIRE(analysis.names.reference((*parameter_reference)->id)->symbol ==
          parameter_definition->symbol);
  REQUIRE(analysis.names.reference((*global_reference)->id)->symbol == global_definition->symbol);
  REQUIRE(analysis.names.reference((*function_reference)->id)->symbol ==
          function_definition->symbol);
  REQUIRE(analysis.names.reference((*local_reference)->id)->symbol.valid());
  REQUIRE(analysis.names.reference((*builtin_reference)->id)->binding ==
          mpf::detail::BindingKind::builtin);
  REQUIRE(analysis.names.reference((*builtin_reference)->id)->intrinsic ==
          mpf::detail::IntrinsicId::absolute);

  auto invalid = analysis.names;
  invalid.symbols[parameter_definition->symbol.value()].scope = invalid.global_scope;
  REQUIRE(!mpf::detail::verify_names(lowered.program, invalid, "bad-scope").empty());
  auto stale = analysis.names;
  ++lowered.program.revision;
  REQUIRE(!mpf::detail::verify_names(lowered.program, stale, "stale").empty());
}

TEST_CASE("TypeScript name analysis owns explicit branch scopes and outer assignments") {
  auto lowered = lower_source(mpf::SourceLanguage::typescript,
                              "let value: number = 1;\n"
                              "if (true) {\n"
                              "  let value: string = \"inner\";\n"
                              "  console.log(value);\n"
                              "} else {\n"
                              "  value = 42;\n"
                              "}\n"
                              "console.log(value);\n",
                              "scopes.ts");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  REQUIRE(mpf::detail::verify_names(lowered.program, analysis.names, "typescript-scope").empty());

  const auto& global = lowered.program.statements[0];
  const auto& conditional = lowered.program.statements[1];
  const auto& local = conditional.body[0];
  const auto& outer_assignment = conditional.alternative[0];
  const auto* global_definition = analysis.names.use(global.id, mpf::detail::NameRole::declaration);
  const auto* local_definition = analysis.names.use(local.id, mpf::detail::NameRole::declaration);
  const auto* assignment =
      analysis.names.use(outer_assignment.id, mpf::detail::NameRole::assignment);
  REQUIRE(global_definition != nullptr);
  REQUIRE(local_definition != nullptr);
  REQUIRE(assignment != nullptr);
  REQUIRE(global_definition->symbol != local_definition->symbol);
  REQUIRE(assignment->symbol == global_definition->symbol);

  const auto body_scope = analysis.names.body_scope(conditional.id);
  const auto alternative_scope = analysis.names.alternative_scope(conditional.id);
  REQUIRE(body_scope.valid());
  REQUIRE(alternative_scope.valid());
  REQUIRE(analysis.names.scope(body_scope)->parent == analysis.names.global_scope);
  REQUIRE(analysis.names.scope(alternative_scope)->parent == analysis.names.global_scope);
  REQUIRE(analysis.names.symbol(local_definition->symbol)->scope == body_scope);
  REQUIRE(assignment->scope == alternative_scope);

  std::vector<const mpf::detail::hir::Expression*> identifiers;
  collect_identifiers(lowered.program.statements, identifiers);
  const auto inner =
      std::find_if(identifiers.begin(), identifiers.end(), [](const auto* expression) {
        return expression->value == "value" && expression->location.line == 4;
      });
  const auto outer =
      std::find_if(identifiers.begin(), identifiers.end(), [](const auto* expression) {
        return expression->value == "value" && expression->location.line == 8;
      });
  REQUIRE(inner != identifiers.end());
  REQUIRE(outer != identifiers.end());
  REQUIRE(analysis.names.reference((*inner)->id)->symbol == local_definition->symbol);
  REQUIRE(analysis.names.reference((*outer)->id)->symbol == global_definition->symbol);

  auto corrupted = analysis.names;
  corrupted.scope_edges[conditional.id.value()].body = alternative_scope;
  REQUIRE(!mpf::detail::verify_names(lowered.program, corrupted, "corrupt-scope").empty());
  auto aliased = analysis.names;
  aliased.scope_edges[global.id.value()].body = body_scope;
  REQUIRE(!mpf::detail::verify_names(lowered.program, aliased, "aliased-scope-edge").empty());
}

TEST_CASE("TypeScript for lowering owns initializer condition update and continue CFG blocks") {
  auto lowered = lower_source(mpf::SourceLanguage::typescript,
                              "let total: number = 0;\n"
                              "for (let index: number = 0; index < 4; index++) {\n"
                              "  if (index === 2) { continue; }\n"
                              "  total = total + index;\n"
                              "}\n",
                              "for_cfg.ts");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  REQUIRE(mpf::detail::mir::verify(mir.program, "typescript-for-cfg").empty());

  const mpf::detail::mir::Statement* loop = nullptr;
  const mpf::detail::mir::Statement* continuation = nullptr;
  for (std::size_t index = 1; index < mir.program.statements.size(); ++index) {
    const auto& statement = mir.program.statements[index];
    if (statement.kind == mpf::detail::StatementKind::for_loop) loop = &statement;
    if (statement.kind == mpf::detail::StatementKind::continue_statement) continuation = &statement;
  }
  REQUIRE(loop != nullptr);
  REQUIRE(continuation != nullptr);
  REQUIRE(loop->expression.valid());
  REQUIRE(loop->secondary_expression.valid());
  REQUIRE(loop->tertiary_expression.valid());
  REQUIRE(mir.program.instructions[loop->instruction.value()].opcode ==
          mpf::detail::mir::Opcode::store);

  const auto block_for_instruction = [&](const mpf::detail::InstructionId instruction) {
    for (std::size_t index = 1; index < mir.program.blocks.size(); ++index) {
      const auto& block = mir.program.blocks[index];
      if (std::find(block.instructions.begin(), block.instructions.end(), instruction) !=
          block.instructions.end()) {
        return block.id;
      }
    }
    return mpf::detail::BlockId{};
  };
  const auto* condition = mpf::detail::mir::expression(mir.program, loop->secondary_expression);
  const auto* update = mpf::detail::mir::expression(mir.program, loop->tertiary_expression);
  REQUIRE(condition != nullptr);
  REQUIRE(update != nullptr);
  const auto condition_block = block_for_instruction(condition->instruction);
  const auto update_block = block_for_instruction(update->instruction);
  const auto continue_block = block_for_instruction(continuation->instruction);
  REQUIRE(condition_block.valid());
  REQUIRE(update_block.valid());
  REQUIRE(continue_block.valid());
  REQUIRE(mir.program.blocks[condition_block.value()].terminator.kind ==
          mpf::detail::mir::TerminatorKind::conditional_branch);
  REQUIRE(std::find(mir.program.blocks[continue_block.value()].terminator.successors.begin(),
                    mir.program.blocks[continue_block.value()].terminator.successors.end(),
                    update_block) !=
          mir.program.blocks[continue_block.value()].terminator.successors.end());
  REQUIRE(std::any_of(mir.program.blocks[update_block.value()].instructions.begin(),
                      mir.program.blocks[update_block.value()].instructions.end(),
                      [&](const mpf::detail::InstructionId instruction) {
                        return mir.program.instructions[instruction.value()].opcode ==
                               mpf::detail::mir::Opcode::store;
                      }));

  auto direct_continue = lower_source(mpf::SourceLanguage::typescript,
                                      "for (let index: number = 0; index < 2; index++) {\n"
                                      "  continue;\n"
                                      "}\n",
                                      "for_direct_continue.ts");
  auto direct_analysis =
      mpf::detail::analyze_program(direct_continue.program, std::move(direct_continue.semantics));
  REQUIRE(direct_analysis.empty());
  auto direct_mir =
      mpf::detail::mir::lower_from_hir(std::move(direct_continue.program),
                                       std::move(direct_analysis.semantics), direct_analysis.names);
  REQUIRE(direct_mir.diagnostics.empty());
  const auto direct_loop =
      std::find_if(direct_mir.program.statements.begin() + 1, direct_mir.program.statements.end(),
                   [](const mpf::detail::mir::Statement& statement) {
                     return statement.kind == mpf::detail::StatementKind::for_loop;
                   });
  REQUIRE(direct_loop != direct_mir.program.statements.end());
  const auto* direct_update =
      mpf::detail::mir::expression(direct_mir.program, direct_loop->tertiary_expression);
  REQUIRE(direct_update != nullptr);
  const auto direct_update_block = [&]() {
    for (std::size_t index = 1; index < direct_mir.program.blocks.size(); ++index) {
      const auto& block = direct_mir.program.blocks[index];
      if (std::find(block.instructions.begin(), block.instructions.end(),
                    direct_update->instruction) != block.instructions.end()) {
        return block.id;
      }
    }
    return mpf::detail::BlockId{};
  }();
  REQUIRE(direct_update_block.valid());
  REQUIRE(!direct_mir.program.blocks[direct_update_block.value()].arguments.empty());
  REQUIRE(mpf::detail::mir::verify(direct_mir.program, "typescript-direct-continue").empty());
  const auto direct_optimized =
      mpf::detail::mir::run_default_optimization_pipeline(direct_mir.program);
  REQUIRE(direct_optimized.diagnostics.empty());
  REQUIRE(direct_optimized.statistics.propagated_block_arguments != 0U);
  REQUIRE(mpf::detail::mir::verify(direct_mir.program, "typescript-for-optimized").empty());

  std::size_t predecessor_index = 0U;
  std::size_t successor_index = 0U;
  std::size_t owner_index = 0U;
  mpf::detail::BlockId forwarded_target;
  for (std::size_t function_index = 1;
       function_index < direct_mir.program.functions.size() && !forwarded_target.valid();
       ++function_index) {
    for (const auto block_id : direct_mir.program.functions[function_index].blocks) {
      const auto& terminator = direct_mir.program.blocks[block_id.value()].terminator;
      for (std::size_t edge = 0; edge < terminator.successors.size(); ++edge) {
        const auto target = terminator.successors[edge];
        if (terminator.successor_arguments[edge].empty() &&
            direct_mir.program.blocks[target.value()].arguments.empty()) {
          predecessor_index = block_id.value();
          successor_index = edge;
          owner_index = function_index;
          forwarded_target = target;
          break;
        }
      }
      if (forwarded_target.valid()) break;
    }
  }
  REQUIRE(forwarded_target.valid());
  const auto forwarding_id = mpf::detail::BlockId{
      static_cast<mpf::detail::BlockId::value_type>(direct_mir.program.blocks.size())};
  mpf::detail::mir::BasicBlock forwarding;
  forwarding.id = forwarding_id;
  forwarding.terminator.kind = mpf::detail::mir::TerminatorKind::branch;
  forwarding.terminator.successors = {forwarded_target};
  forwarding.terminator.successor_arguments = {{}};
  direct_mir.program.blocks.push_back(std::move(forwarding));
  direct_mir.program.blocks[predecessor_index].terminator.successors[successor_index] =
      forwarding_id;
  direct_mir.program.functions[owner_index].blocks.push_back(forwarding_id);
  REQUIRE(mpf::detail::mir::verify(direct_mir.program, "forwarding-input").empty());
  const auto forwarding_optimized =
      mpf::detail::mir::run_default_optimization_pipeline(direct_mir.program);
  REQUIRE(forwarding_optimized.diagnostics.empty());
  REQUIRE(forwarding_optimized.statistics.removed_blocks == 1U);
  REQUIRE(mpf::detail::mir::verify(direct_mir.program, "forwarding-output").empty());
  auto corrupt_for = direct_mir.program;
  const auto corrupt_loop =
      std::find_if(corrupt_for.statements.begin() + 1, corrupt_for.statements.end(),
                   [](const mpf::detail::mir::Statement& statement) {
                     return statement.kind == mpf::detail::StatementKind::for_loop;
                   });
  REQUIRE(corrupt_loop != corrupt_for.statements.end());
  corrupt_loop->has_tertiary_expression = false;
  REQUIRE(!mpf::detail::mir::verify(corrupt_for, "typescript-corrupt-for").empty());
}

TEST_CASE("HIR lowers to typed CFG MIR with shape storage and effects") {
  auto lowered = lower_python(
      "value = [1, 2, 3]\n"
      "counter = 0\n"
      "if value[0] > 0:\n"
      "    counter = 1\n"
      "else:\n"
      "    counter = 2\n"
      "while counter < 4:\n"
      "    counter = counter + 1\n"
      "view = value[1:]\n"
      "print(counter)\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  const auto alias_effects = mpf::detail::mir::analyze_alias_effects(mir.program);
  REQUIRE(mir.program.functions.size() > 1);
  REQUIRE(mir.program.blocks.size() > 2);
  REQUIRE(mir.program.instructions.size() > 1);
  REQUIRE(mir.program.expressions.size() > 1);
  REQUIRE(mir.program.statements.size() > 1);
  REQUIRE(!mir.program.roots.empty());
  REQUIRE(mir.program.types.size() > 1);
  REQUIRE(mir.program.shapes.size() > 1);
  REQUIRE(mir.program.storages.size() > 1);
  REQUIRE(!alias_effects.aliases.empty());
  REQUIRE(std::any_of(mir.program.storages.begin() + 1, mir.program.storages.end(),
                      [](const mpf::detail::mir::StorageData& storage) {
                        return storage.kind == mpf::detail::mir::StorageKind::view &&
                               storage.base.valid();
                      }));
  const auto view = std::find_if(mir.program.storages.begin() + 1, mir.program.storages.end(),
                                 [](const mpf::detail::mir::StorageData& storage) {
                                   return storage.kind == mpf::detail::mir::StorageKind::view &&
                                          storage.base.valid();
                                 });
  REQUIRE(view != mir.program.storages.end());
  const auto view_id = mpf::detail::StorageId{
      static_cast<mpf::detail::StorageId::value_type>(view - mir.program.storages.begin())};
  REQUIRE(mpf::detail::mir::alias_between(alias_effects, view->base, view_id) ==
          mpf::detail::mir::AliasClass::may_alias);
  REQUIRE(mpf::detail::mir::alias_between(alias_effects, view_id, view_id) ==
          mpf::detail::mir::AliasClass::must_alias);
  const auto value = std::find_if(
      mir.program.storages.begin() + 1, mir.program.storages.end(),
      [](const mpf::detail::mir::StorageData& storage) { return storage.name == "value"; });
  const auto counter = std::find_if(
      mir.program.storages.begin() + 1, mir.program.storages.end(),
      [](const mpf::detail::mir::StorageData& storage) { return storage.name == "counter"; });
  REQUIRE(value != mir.program.storages.end());
  REQUIRE(counter != mir.program.storages.end());
  const auto value_id = mpf::detail::StorageId{
      static_cast<mpf::detail::StorageId::value_type>(value - mir.program.storages.begin())};
  const auto counter_id = mpf::detail::StorageId{
      static_cast<mpf::detail::StorageId::value_type>(counter - mir.program.storages.begin())};
  REQUIRE(mpf::detail::mir::alias_between(alias_effects, value_id, counter_id) ==
          mpf::detail::mir::AliasClass::no_alias);
  REQUIRE(std::any_of(
      mir.program.blocks.begin() + 1, mir.program.blocks.end(),
      [](const mpf::detail::mir::BasicBlock& block) { return !block.arguments.empty(); }));
  for (const auto& block : mir.program.blocks) {
    REQUIRE(block.terminator.successors.size() == block.terminator.successor_arguments.size());
    for (std::size_t edge = 0; edge < block.terminator.successors.size(); ++edge) {
      const auto target = block.terminator.successors[edge];
      REQUIRE(target.valid());
      REQUIRE(block.terminator.successor_arguments[edge].size() ==
              mir.program.blocks[target.value()].arguments.size());
    }
  }
  REQUIRE(std::any_of(alias_effects.instructions.begin() + 1, alias_effects.instructions.end(),
                      [](const mpf::detail::mir::InstructionEffectFacts& instruction) {
                        return mpf::detail::mir::has_effect(instruction.effects,
                                                            mpf::detail::mir::Effect::io);
                      }));
  REQUIRE(mpf::detail::mir::verify(mir.program, "test").empty());
  REQUIRE(mpf::detail::mir::verify_alias_effects(mir.program, alias_effects, "test").empty());
}

TEST_CASE("MIR owns dense flat value and operation arenas tied to instructions") {
  auto lowered = lower_python(
      "value = 1\n"
      "if value > 0:\n"
      "    print(value)\n"
      "else:\n"
      "    print(0)\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto lowered_mir = mpf::detail::mir::lower_from_hir(
      std::move(lowered.program), std::move(analysis.semantics), analysis.names);
  REQUIRE(lowered_mir.diagnostics.empty());
  const auto& program = lowered_mir.program;
  REQUIRE(program.expressions.front().id == mpf::detail::MirExpressionId{});
  REQUIRE(program.statements.front().id == mpf::detail::MirStatementId{});
  REQUIRE(program.attributes.mir_revision == program.revision);
  REQUIRE(program.attributes.expression_count + 1U == program.expressions.size());
  REQUIRE(program.attributes.statement_count + 1U == program.statements.size());
  REQUIRE(program.attributes.instruction_count + 1U == program.instructions.size());
  REQUIRE(program.attributes.expressions.size() == program.expressions.size());
  REQUIRE(program.attributes.statements.size() == program.statements.size());
  REQUIRE(program.attributes.instructions.size() == program.instructions.size());
  for (std::size_t index = 1; index < program.expressions.size(); ++index) {
    const auto& expression = program.expressions[index];
    REQUIRE(expression.id.value() == index);
    REQUIRE(expression.instruction.valid());
    REQUIRE(expression.instruction.value() < program.instructions.size());
    REQUIRE(program.instructions[expression.instruction.value()].result == expression.value_id);
    REQUIRE(program.attributes.expressions[index].origin == expression.id);
  }
  for (std::size_t index = 1; index < program.statements.size(); ++index) {
    const auto& statement = program.statements[index];
    REQUIRE(statement.id.value() == index);
    REQUIRE(statement.instruction.valid());
    REQUIRE(statement.instruction.value() < program.instructions.size());
    REQUIRE(program.attributes.statements[index].origin == statement.id);
  }
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    REQUIRE(program.attributes.instructions[index].origin == program.instructions[index].id);
  }

  auto bad_expression_edge = program;
  const auto parent = std::find_if(
      bad_expression_edge.expressions.begin() + 1, bad_expression_edge.expressions.end(),
      [](const mpf::detail::mir::Expression& expression) { return !expression.children.empty(); });
  REQUIRE(parent != bad_expression_edge.expressions.end());
  parent->children.front() = mpf::detail::MirExpressionId{999999};
  REQUIRE(!mpf::detail::mir::verify(bad_expression_edge, "bad-expression-arena").empty());

  auto bad_operation_owner = program;
  REQUIRE(!bad_operation_owner.roots.empty());
  bad_operation_owner.roots.push_back(bad_operation_owner.roots.front());
  REQUIRE(!mpf::detail::mir::verify(bad_operation_owner, "bad-operation-arena").empty());

  auto stale_attributes = program;
  ++stale_attributes.revision;
  REQUIRE(!mpf::detail::mir::verify(stale_attributes, "stale-operation-attributes").empty());

  auto invalid_attribute_origin = program;
  invalid_attribute_origin.attributes.expressions[1].origin = mpf::detail::MirExpressionId{999999};
  REQUIRE(!mpf::detail::mir::verify(invalid_attribute_origin, "bad-attribute-origin").empty());

  auto invalid_attribute_type = program;
  invalid_attribute_type.attributes.statements[1].previous_type = mpf::detail::TypeId{999999};
  REQUIRE(!mpf::detail::mir::verify(invalid_attribute_type, "bad-attribute-type").empty());

  auto invalid_instruction_attribute_origin = program;
  invalid_instruction_attribute_origin.attributes.instructions[1].origin =
      mpf::detail::InstructionId{999999};
  REQUIRE(!mpf::detail::mir::verify(invalid_instruction_attribute_origin,
                                    "bad-instruction-attribute-origin")
               .empty());

  auto invalid_memory_access = program;
  const auto memory_instruction =
      std::find_if(invalid_memory_access.attributes.instructions.begin() + 1,
                   invalid_memory_access.attributes.instructions.end(),
                   [](const mpf::detail::mir::InstructionAttributes& attributes) {
                     return !attributes.memory_accesses.empty();
                   });
  REQUIRE(memory_instruction != invalid_memory_access.attributes.instructions.end());
  memory_instruction->memory_accesses.front().mode = mpf::detail::mir::MemoryAccessMode::none;
  REQUIRE(!mpf::detail::mir::verify(invalid_memory_access, "bad-memory-access").empty());
}

TEST_CASE("MIR owns lazy evaluation CFG and explicit memory operations") {
  auto lowered = lower_python(
      "def probe(value):\n"
      "    print(value)\n"
      "    return value\n"
      "first = 1 < probe(2) < 3\n"
      "choice = probe(4) if first and probe(5) else probe(0)\n"
      "print(choice)\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto lowered_mir = mpf::detail::mir::lower_from_hir(
      std::move(lowered.program), std::move(analysis.semantics), analysis.names);
  REQUIRE(lowered_mir.diagnostics.empty());
  const auto& program = lowered_mir.program;

  REQUIRE(std::count_if(program.attributes.expressions.begin() + 1,
                        program.attributes.expressions.end(),
                        [](const mpf::detail::mir::ExpressionAttributes& attributes) {
                          return attributes.lazy_cfg;
                        }) >= 3);
  REQUIRE(std::any_of(program.instructions.begin() + 1, program.instructions.end(),
                      [](const mpf::detail::mir::Instruction& instruction) {
                        return instruction.opcode == mpf::detail::mir::Opcode::truthiness;
                      }));
  REQUIRE(std::any_of(program.instructions.begin() + 1, program.instructions.end(),
                      [](const mpf::detail::mir::Instruction& instruction) {
                        return instruction.opcode == mpf::detail::mir::Opcode::compare &&
                               instruction.comparison != mpf::detail::ComparisonOperator::none;
                      }));
  REQUIRE(std::count_if(program.blocks.begin() + 1, program.blocks.end(),
                        [](const mpf::detail::mir::BasicBlock& block) {
                          return block.terminator.kind ==
                                 mpf::detail::mir::TerminatorKind::conditional_branch;
                        }) >= 3);
  REQUIRE(std::any_of(
      program.expressions.begin() + 1, program.expressions.end(),
      [&](const mpf::detail::mir::Expression& expression) {
        const auto* attributes = mpf::detail::mir::attributes(program, expression.id);
        if (attributes == nullptr || !attributes->lazy_cfg) return false;
        const auto& instruction = program.instructions[expression.instruction.value()];
        if (instruction.operands.size() != 1U) return false;
        return std::any_of(program.blocks.begin() + 1, program.blocks.end(),
                           [&](const mpf::detail::mir::BasicBlock& block) {
                             return std::any_of(
                                 block.arguments.begin(), block.arguments.end(),
                                 [&](const mpf::detail::mir::BlockArgument& argument) {
                                   return argument.value == instruction.operands.front();
                                 });
                           });
      }));
  REQUIRE(std::any_of(program.instructions.begin() + 1, program.instructions.end(),
                      [](const mpf::detail::mir::Instruction& instruction) {
                        return instruction.opcode == mpf::detail::mir::Opcode::load;
                      }));
  REQUIRE(std::any_of(program.instructions.begin() + 1, program.instructions.end(),
                      [](const mpf::detail::mir::Instruction& instruction) {
                        return instruction.opcode == mpf::detail::mir::Opcode::store;
                      }));

  auto invalid_lazy_operand = program;
  const auto lazy = std::find_if(
      invalid_lazy_operand.expressions.begin() + 1, invalid_lazy_operand.expressions.end(),
      [&](const mpf::detail::mir::Expression& expression) {
        const auto* attributes = mpf::detail::mir::attributes(invalid_lazy_operand, expression.id);
        return attributes != nullptr && attributes->lazy_cfg;
      });
  REQUIRE(lazy != invalid_lazy_operand.expressions.end());
  invalid_lazy_operand.attributes.expressions[lazy->id.value()].lazy_cfg = false;
  REQUIRE(!mpf::detail::mir::verify(invalid_lazy_operand, "bad-lazy-operand").empty());

  auto invalid_truthiness = program;
  const auto truthiness = std::find_if(
      invalid_truthiness.instructions.begin() + 1, invalid_truthiness.instructions.end(),
      [](const mpf::detail::mir::Instruction& instruction) {
        return instruction.opcode == mpf::detail::mir::Opcode::truthiness;
      });
  REQUIRE(truthiness != invalid_truthiness.instructions.end());
  truthiness->operands.clear();
  REQUIRE(!mpf::detail::mir::verify(invalid_truthiness, "bad-truthiness").empty());

  auto invalid_comparison = program;
  const auto comparison = std::find_if(
      invalid_comparison.instructions.begin() + 1, invalid_comparison.instructions.end(),
      [](const mpf::detail::mir::Instruction& instruction) {
        return instruction.opcode == mpf::detail::mir::Opcode::compare;
      });
  REQUIRE(comparison != invalid_comparison.instructions.end());
  comparison->comparison = mpf::detail::ComparisonOperator::none;
  REQUIRE(!mpf::detail::mir::verify(invalid_comparison, "bad-comparison").empty());
}

TEST_CASE("MIR interns tuple function and reference signatures across call sites") {
  auto python = lower_python(
      "def pair(value):\n"
      "    next_value = value + 1\n"
      "    return value, next_value\n"
      "seed = 20\n"
      "left, right = pair(seed)\n"
      "print(left + right)\n");
  auto python_analysis = mpf::detail::analyze_program(python.program, std::move(python.semantics));
  REQUIRE(python_analysis.empty());
  auto python_mir = mpf::detail::mir::lower_from_hir(
      std::move(python.program), std::move(python_analysis.semantics), python_analysis.names);
  REQUIRE(python_mir.diagnostics.empty());
  const auto python_effects = mpf::detail::mir::analyze_alias_effects(python_mir.program);
  const auto pair = std::find_if(
      python_mir.program.functions.begin() + 1, python_mir.program.functions.end(),
      [](const mpf::detail::mir::Function& function) { return function.name == "pair"; });
  REQUIRE(pair != python_mir.program.functions.end());
  REQUIRE(pair->signature.valid());
  REQUIRE(pair->parameter_shapes.size() == pair->parameter_types.size());
  REQUIRE(pair->result_shapes.size() == pair->result_types.size());
  const auto& pair_signature = python_mir.program.types[pair->signature.value()];
  REQUIRE(pair_signature.kind == mpf::detail::mir::TypeKind::function);
  REQUIRE(pair_signature.parameters.size() == 1U);
  REQUIRE(pair_signature.results.size() == 1U);
  const auto& pair_result = python_mir.program.types[pair_signature.results.front().value()];
  REQUIRE(pair_result.kind == mpf::detail::mir::TypeKind::tuple);
  REQUIRE(pair_result.elements.size() == 2U);
  REQUIRE(python_mir.program.calls.size() == 1U);
  REQUIRE(python_mir.program.calls.front().callee == pair->id);
  REQUIRE(python_mir.program.calls.front().requested_results == 1U);
  const auto* pair_effects = python_effects.function(pair->id);
  REQUIRE(pair_effects != nullptr);
  REQUIRE(pair_effects->parameter_reads.size() == 1U);
  REQUIRE(pair_effects->parameter_reads.front());
  REQUIRE(!pair_effects->parameter_writes.front());
  REQUIRE(python_effects.calls.size() == 1U);
  REQUIRE(python_effects.call(python_mir.program.calls.front().instruction) != nullptr);
  REQUIRE(!mpf::detail::mir::has_effect(python_effects.calls.front().effects,
                                        mpf::detail::mir::Effect::external_unknown));
  REQUIRE(!python_effects.calls.front().reads.empty());
  REQUIRE(python_effects.calls.front().writes.empty());
  REQUIRE(mpf::detail::dump_mir(python_mir.program).find("call !i") != std::string::npos);

  auto fortran = lower_source(mpf::SourceLanguage::fortran,
                              "program reference_contract\n"
                              "  integer :: value = 41\n"
                              "  call bump(value)\n"
                              "contains\n"
                              "  subroutine bump(value)\n"
                              "    integer, intent(inout) :: value\n"
                              "    value = value + 1\n"
                              "  end subroutine bump\n"
                              "end program reference_contract\n",
                              "reference_contract.f90");
  auto fortran_analysis =
      mpf::detail::analyze_program(fortran.program, std::move(fortran.semantics));
  REQUIRE(fortran_analysis.empty());
  auto fortran_mir = mpf::detail::mir::lower_from_hir(
      std::move(fortran.program), std::move(fortran_analysis.semantics), fortran_analysis.names);
  REQUIRE(fortran_mir.diagnostics.empty());
  const auto fortran_effects = mpf::detail::mir::analyze_alias_effects(fortran_mir.program);
  const auto bump = std::find_if(
      fortran_mir.program.functions.begin() + 1, fortran_mir.program.functions.end(),
      [](const mpf::detail::mir::Function& function) { return function.name == "bump"; });
  REQUIRE(bump != fortran_mir.program.functions.end());
  const auto& bump_signature = fortran_mir.program.types[bump->signature.value()];
  REQUIRE(bump_signature.parameters.size() == 1U);
  const auto& reference = fortran_mir.program.types[bump_signature.parameters.front().value()];
  REQUIRE(reference.kind == mpf::detail::mir::TypeKind::reference);
  REQUIRE(reference.reference_intent == mpf::detail::ParameterIntent::inout);
  REQUIRE(reference.referent == bump->parameter_types.front());
  REQUIRE(fortran_mir.program.calls.size() == 1U);
  REQUIRE(fortran_mir.program.calls.front().arguments.front().storage.valid());
  REQUIRE(fortran_mir.program.calls.front().arguments.front().root.valid());
  REQUIRE(fortran_mir.program.calls.front().arguments.front().transfer ==
          mpf::detail::ArgumentTransfer::mutable_borrow_inout);
  REQUIRE(fortran_mir.program.calls.front().arguments.front().writable);
  const auto* bump_effects = fortran_effects.function(bump->id);
  REQUIRE(bump_effects != nullptr);
  REQUIRE(bump_effects->parameter_reads.front());
  REQUIRE(bump_effects->parameter_writes.front());
  REQUIRE(fortran_effects.calls.size() == 1U);
  REQUIRE(!fortran_effects.calls.front().reads.empty());
  REQUIRE(!fortran_effects.calls.front().writes.empty());

  auto invalid_signature = fortran_mir.program;
  invalid_signature.functions[bump->id.value()].signature = bump->parameter_types.front();
  REQUIRE(!mpf::detail::mir::verify(invalid_signature, "bad-signature").empty());

  auto invalid_reference_actual = fortran_mir.program;
  invalid_reference_actual.calls.front().arguments.front().storage = {};
  REQUIRE(!mpf::detail::mir::verify(invalid_reference_actual, "bad-reference").empty());

  auto invalid_transfer = fortran_mir.program;
  invalid_transfer.calls.front().arguments.front().transfer =
      mpf::detail::ArgumentTransfer::read_only_borrow;
  REQUIRE(!mpf::detail::mir::verify(invalid_transfer, "bad-transfer").empty());

  auto missing_call_edge = fortran_mir.program;
  missing_call_edge.calls.clear();
  REQUIRE(!mpf::detail::mir::verify(missing_call_edge, "missing-call-edge").empty());
}

TEST_CASE("storage regions prove exact N-dimensional and strided relationships") {
  using mpf::detail::StorageRegion;
  using mpf::detail::StorageRegionDimension;
  using mpf::detail::StorageRegionKind;
  using mpf::detail::StorageRegionRelation;

  const auto full = mpf::detail::full_storage_region({2U, 4U});
  REQUIRE(mpf::detail::valid_storage_region(full));
  REQUIRE(full.kind == StorageRegionKind::rectangular);
  REQUIRE((full.dimensions == std::vector<StorageRegionDimension>{{0U, 1U, 2U}, {0U, 1U, 4U}}));
  REQUIRE(mpf::detail::storage_region_relation(full, full) == StorageRegionRelation::identical);

  const StorageRegion first_columns{
      StorageRegionKind::rectangular, {2U, 4U}, {{0U, 1U, 2U}, {0U, 1U, 2U}}};
  const StorageRegion last_columns{
      StorageRegionKind::rectangular, {2U, 4U}, {{0U, 1U, 2U}, {2U, 1U, 2U}}};
  REQUIRE(mpf::detail::valid_storage_region(first_columns));
  REQUIRE(mpf::detail::valid_storage_region(last_columns));
  REQUIRE(mpf::detail::storage_region_relation(first_columns, last_columns) ==
          StorageRegionRelation::disjoint);

  const StorageRegion odd{StorageRegionKind::rectangular, {6U}, {{0U, 2U, 3U}}};
  const StorageRegion even{StorageRegionKind::rectangular, {6U}, {{1U, 2U, 3U}}};
  const StorageRegion odd_tail{StorageRegionKind::rectangular, {6U}, {{2U, 2U, 2U}}};
  REQUIRE(mpf::detail::storage_region_relation(odd, even) == StorageRegionRelation::disjoint);
  REQUIRE(mpf::detail::storage_region_relation(odd, odd_tail) == StorageRegionRelation::overlaps);

  const StorageRegion linear_odd{StorageRegionKind::linearized, {2U, 4U}, {{0U, 2U, 4U}}};
  const StorageRegion linear_even{StorageRegionKind::linearized, {2U, 4U}, {{1U, 2U, 4U}}};
  REQUIRE(mpf::detail::storage_region_relation(linear_odd, linear_even) ==
          StorageRegionRelation::disjoint);

  const StorageRegion empty{StorageRegionKind::rectangular, {6U}, {{0U, 1U, 0U}}};
  const StorageRegion invalid{StorageRegionKind::rectangular, {6U}, {{5U, 2U, 2U}}};
  REQUIRE(mpf::detail::empty_storage_region(empty));
  REQUIRE(mpf::detail::storage_region_relation(empty, odd) == StorageRegionRelation::disjoint);
  REQUIRE(!mpf::detail::valid_storage_region(invalid));
  REQUIRE(mpf::detail::storage_region_relation(StorageRegion{}, odd) ==
          StorageRegionRelation::unknown);
}

TEST_CASE("MIR call regions model borrow copy forwarding lifetime and overlap") {
  auto lowered = lower_source(mpf::SourceLanguage::fortran,
                              "program transfer_contract\n"
                              "integer :: values(5) = [1,2,3,4,5]\n"
                              "call bump(values(2))\n"
                              "call adjust(values(1:5:2))\n"
                              "call replace(values(2:4))\n"
                              "contains\n"
                              "subroutine bump(value)\n"
                              "integer, intent(inout) :: value\n"
                              "value = value + 1\n"
                              "end subroutine bump\n"
                              "subroutine adjust(items)\n"
                              "integer, intent(inout) :: items(:)\n"
                              "items(1) = items(1) + 1\n"
                              "end subroutine adjust\n"
                              "subroutine replace(items)\n"
                              "integer, intent(out) :: items(:)\n"
                              "items(1) = 9\n"
                              "end subroutine replace\n"
                              "end program transfer_contract\n",
                              "transfer_contract.f90");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  REQUIRE(mir.program.calls.size() == 3U);
  const auto& element = mir.program.calls[0].arguments.front();
  const auto& section = mir.program.calls[1].arguments.front();
  const auto& output_section = mir.program.calls[2].arguments.front();
  REQUIRE(element.transfer == mpf::detail::ArgumentTransfer::mutable_borrow_inout);
  REQUIRE(element.view == mpf::detail::mir::StorageViewKind::element);
  REQUIRE(section.transfer == mpf::detail::ArgumentTransfer::copy_in_out);
  REQUIRE(section.view == mpf::detail::mir::StorageViewKind::section);
  REQUIRE(section.lifetime == mpf::detail::mir::StorageLifetime::expression);
  REQUIRE(output_section.transfer == mpf::detail::ArgumentTransfer::copy_out);
  REQUIRE(element.root == section.root);
  const auto copy =
      std::find_if(mir.program.instructions.begin() + 1, mir.program.instructions.end(),
                   [](const mpf::detail::mir::Instruction& instruction) {
                     return instruction.opcode == mpf::detail::mir::Opcode::copy;
                   });
  const auto writeback =
      std::find_if(mir.program.instructions.begin() + 1, mir.program.instructions.end(),
                   [](const mpf::detail::mir::Instruction& instruction) {
                     return instruction.opcode == mpf::detail::mir::Opcode::writeback;
                   });
  REQUIRE(copy != mir.program.instructions.end());
  REQUIRE(writeback != mir.program.instructions.end());
  REQUIRE(copy->transfer == mpf::detail::ArgumentTransfer::copy_in_out);
  REQUIRE(writeback->transfer == mpf::detail::ArgumentTransfer::copy_in_out);
  REQUIRE(copy->storage.valid());
  REQUIRE(mir.program.storages[copy->storage.value()].kind ==
          mpf::detail::mir::StorageKind::temporary);
  REQUIRE(writeback->storage == section.storage);
  REQUIRE(writeback->operands == std::vector<mpf::detail::ValueId>{copy->result});
  const auto* copy_attributes = mpf::detail::mir::attributes(mir.program, copy->id);
  const auto* writeback_attributes = mpf::detail::mir::attributes(mir.program, writeback->id);
  REQUIRE(copy_attributes != nullptr);
  REQUIRE(writeback_attributes != nullptr);
  REQUIRE(copy_attributes->memory_accesses.size() == 1U);
  REQUIRE(writeback_attributes->memory_accesses.size() == 1U);
  REQUIRE(copy_attributes->memory_accesses.front().region == section.region);
  REQUIRE(writeback_attributes->memory_accesses.front().region == section.region);
  REQUIRE(mpf::detail::mir::memory_access_reads(copy_attributes->memory_accesses.front().mode));
  REQUIRE(
      mpf::detail::mir::memory_access_writes(writeback_attributes->memory_accesses.front().mode));
  const auto output_copy =
      std::find_if(mir.program.instructions.begin() + 1, mir.program.instructions.end(),
                   [](const mpf::detail::mir::Instruction& instruction) {
                     return instruction.opcode == mpf::detail::mir::Opcode::copy &&
                            instruction.transfer == mpf::detail::ArgumentTransfer::copy_out;
                   });
  REQUIRE(output_copy != mir.program.instructions.end());
  REQUIRE(output_copy->operands.empty());
  REQUIRE(mpf::detail::mir::attributes(mir.program, output_copy->id)->memory_accesses.empty());
  const auto transfer_effects = mpf::detail::mir::analyze_alias_effects(mir.program);
  const auto* copy_effects = transfer_effects.instruction(copy->id);
  const auto* writeback_effects = transfer_effects.instruction(writeback->id);
  REQUIRE(copy_effects != nullptr);
  REQUIRE(writeback_effects != nullptr);
  REQUIRE(mpf::detail::mir::has_effect(copy_effects->effects, mpf::detail::mir::Effect::allocate));
  REQUIRE(
      mpf::detail::mir::has_effect(writeback_effects->effects, mpf::detail::mir::Effect::write));
  REQUIRE(copy_effects->memory_accesses == copy_attributes->memory_accesses);
  REQUIRE(writeback_effects->memory_accesses == writeback_attributes->memory_accesses);

  auto invalid_copy = mir.program;
  invalid_copy.instructions[copy->id.value()].transfer = mpf::detail::ArgumentTransfer::value;
  REQUIRE(!mpf::detail::mir::verify(invalid_copy, "bad-copy-transfer").empty());

  auto invalid_writeback = mir.program;
  invalid_writeback.instructions[writeback->id.value()].operands.clear();
  REQUIRE(!mpf::detail::mir::verify(invalid_writeback, "bad-writeback").empty());
  const auto javascript =
      mpf::detail::javascript::lower(mir.program, transfer_effects, mpf::TranspileOptions{});
  const auto cpp = mpf::detail::cpp::lower(mir.program, transfer_effects, mpf::TranspileOptions{});
  REQUIRE(javascript.diagnostics.empty());
  REQUIRE(cpp.diagnostics.empty());
  const auto borrow_plan =
      "transfers [" +
      std::to_string(static_cast<int>(mpf::detail::ArgumentTransfer::mutable_borrow_inout)) + ']';
  const auto copy_plan =
      "transfers [" + std::to_string(static_cast<int>(mpf::detail::ArgumentTransfer::copy_in_out)) +
      ']';
  REQUIRE(javascript.artifact->debug_dump().find(borrow_plan) != std::string::npos);
  REQUIRE(javascript.artifact->debug_dump().find(copy_plan) != std::string::npos);
  REQUIRE(cpp.artifact->debug_dump().find(borrow_plan) != std::string::npos);
  REQUIRE(cpp.artifact->debug_dump().find(copy_plan) != std::string::npos);
  REQUIRE(javascript.artifact->debug_dump().find("function-abis") != std::string::npos);
  REQUIRE(javascript.artifact->debug_dump().find("parameters [1]") != std::string::npos);
  REQUIRE(javascript.artifact->debug_dump().find("temporaries\n  %l") != std::string::npos);
  REQUIRE(cpp.artifact->debug_dump().find("function-abis") != std::string::npos);
  REQUIRE(cpp.artifact->debug_dump().find("parameters [2:") != std::string::npos);
  REQUIRE(cpp.artifact->debug_dump().find("temporaries\n  %l") != std::string::npos);

  auto optional = lower_source(mpf::SourceLanguage::fortran,
                               "program optional_forward\n"
                               "call outer()\n"
                               "contains\n"
                               "subroutine outer(value)\n"
                               "integer, intent(inout), optional :: value\n"
                               "call inner(value)\n"
                               "end subroutine outer\n"
                               "subroutine inner(value)\n"
                               "integer, intent(inout), optional :: value\n"
                               "end subroutine inner\n"
                               "end program optional_forward\n",
                               "optional_forward.f90");
  auto optional_analysis =
      mpf::detail::analyze_program(optional.program, std::move(optional.semantics));
  REQUIRE(optional_analysis.empty());
  auto optional_mir = mpf::detail::mir::lower_from_hir(
      std::move(optional.program), std::move(optional_analysis.semantics), optional_analysis.names);
  REQUIRE(optional_mir.diagnostics.empty());
  REQUIRE(optional_mir.program.calls.size() == 2U);
  REQUIRE(optional_mir.program.calls[0].arguments.front().transfer ==
          mpf::detail::ArgumentTransfer::omitted);
  REQUIRE(optional_mir.program.calls[1].arguments.front().transfer ==
          mpf::detail::ArgumentTransfer::optional_forward_inout);
  const auto forwarded_storage = optional_mir.program.calls[1].arguments.front().storage;
  REQUIRE(optional_mir.program.storages[forwarded_storage.value()].optional);

  auto disjoint = lower_source(mpf::SourceLanguage::fortran,
                               "program precise_regions\n"
                               "integer :: values(6) = [1,2,3,4,5,6]\n"
                               "call update(values(1:6:2), values(2:6:2))\n"
                               "contains\n"
                               "subroutine update(first, second)\n"
                               "integer, intent(inout) :: first(:), second(:)\n"
                               "first(1) = first(1) + 1\n"
                               "second(1) = second(1) + 1\n"
                               "end subroutine update\n"
                               "end program precise_regions\n",
                               "precise_regions.f90");
  auto disjoint_analysis =
      mpf::detail::analyze_program(disjoint.program, std::move(disjoint.semantics));
  REQUIRE(disjoint_analysis.empty());
  auto disjoint_mir = mpf::detail::mir::lower_from_hir(
      std::move(disjoint.program), std::move(disjoint_analysis.semantics), disjoint_analysis.names);
  REQUIRE(disjoint_mir.diagnostics.empty());
  REQUIRE(disjoint_mir.program.calls.size() == 1U);
  REQUIRE(disjoint_mir.program.calls.front().arguments.size() == 2U);
  const auto& first_region = disjoint_mir.program.calls.front().arguments[0].region;
  const auto& second_region = disjoint_mir.program.calls.front().arguments[1].region;
  REQUIRE(first_region.kind == mpf::detail::StorageRegionKind::rectangular);
  REQUIRE((first_region.dimensions.front() == mpf::detail::StorageRegionDimension{0U, 2U, 3U}));
  REQUIRE((second_region.dimensions.front() == mpf::detail::StorageRegionDimension{1U, 2U, 3U}));
  REQUIRE(disjoint_mir.program.calls.front().arguments[0].root ==
          disjoint_mir.program.calls.front().arguments[1].root);
  const auto disjoint_effects = mpf::detail::mir::analyze_alias_effects(disjoint_mir.program);
  REQUIRE(disjoint_effects.calls.front().overlaps.empty());
  const auto* disjoint_call_effects =
      disjoint_effects.instruction(disjoint_mir.program.calls.front().instruction);
  REQUIRE(disjoint_call_effects != nullptr);
  REQUIRE(disjoint_call_effects->memory_accesses.size() == 2U);
  REQUIRE(mpf::detail::mir::alias_between(
              disjoint_effects, disjoint_call_effects->memory_accesses[0],
              disjoint_call_effects->memory_accesses[1]) == mpf::detail::mir::AliasClass::no_alias);
  REQUIRE(!mpf::detail::mir::memory_accesses_conflict(disjoint_effects,
                                                      disjoint_call_effects->memory_accesses[0],
                                                      disjoint_call_effects->memory_accesses[1]));
  auto stale_access = disjoint_call_effects->memory_accesses[0];
  stale_access.root = {};
  REQUIRE(mpf::detail::mir::alias_between(disjoint_effects, stale_access,
                                          disjoint_call_effects->memory_accesses[1]) ==
          mpf::detail::mir::AliasClass::may_alias);
  REQUIRE(mpf::detail::mir::memory_accesses_conflict(disjoint_effects, stale_access,
                                                     disjoint_call_effects->memory_accesses[1]));
  REQUIRE(mpf::detail::mir::verify_alias_effects(disjoint_mir.program, disjoint_effects,
                                                 "disjoint-regions")
              .empty());
  REQUIRE(mpf::detail::dump_mir(disjoint_mir.program)
              .find("region={kind=1 shape=[6] dimensions=[0:2:3]}") != std::string::npos);

  auto invalid_instruction_region = disjoint_mir.program;
  const auto region_instruction =
      std::find_if(invalid_instruction_region.attributes.instructions.begin() + 1,
                   invalid_instruction_region.attributes.instructions.end(),
                   [](const mpf::detail::mir::InstructionAttributes& attributes) {
                     return !attributes.memory_accesses.empty() &&
                            attributes.memory_accesses.front().region.kind !=
                                mpf::detail::StorageRegionKind::unknown;
                   });
  REQUIRE(region_instruction != invalid_instruction_region.attributes.instructions.end());
  region_instruction->memory_accesses.front().region.dimensions.front().stride = 0U;
  REQUIRE(!mpf::detail::mir::verify(invalid_instruction_region, "invalid-memory-region").empty());

  auto stale_region = disjoint_mir.program;
  stale_region.calls.front().arguments[1].region = stale_region.calls.front().arguments[0].region;
  REQUIRE(!mpf::detail::mir::verify(stale_region, "stale-call-region").empty());
  const auto stale_region_effects = mpf::detail::mir::analyze_alias_effects(stale_region);
  REQUIRE(!stale_region_effects.calls.front().overlaps.empty());
  REQUIRE(stale_region_effects.calls.front().overlaps.front().writable_conflict);

  auto distinct = lower_source(mpf::SourceLanguage::fortran,
                               "program overlap_contract\n"
                               "integer :: left = 1\n"
                               "integer :: right = 2\n"
                               "call update(left, right)\n"
                               "contains\n"
                               "subroutine update(first, second)\n"
                               "integer, intent(inout) :: first, second\n"
                               "first = first + 1\n"
                               "second = second + 1\n"
                               "end subroutine update\n"
                               "end program overlap_contract\n",
                               "overlap_contract.f90");
  auto distinct_analysis =
      mpf::detail::analyze_program(distinct.program, std::move(distinct.semantics));
  REQUIRE(distinct_analysis.empty());
  auto distinct_mir = mpf::detail::mir::lower_from_hir(
      std::move(distinct.program), std::move(distinct_analysis.semantics), distinct_analysis.names);
  REQUIRE(distinct_mir.diagnostics.empty());
  auto corrupted = distinct_mir.program;
  corrupted.calls.front().arguments[1].storage = corrupted.calls.front().arguments[0].storage;
  corrupted.calls.front().arguments[1].root = corrupted.calls.front().arguments[0].root;
  const auto overlap_facts = mpf::detail::mir::analyze_alias_effects(corrupted);
  REQUIRE(!overlap_facts.calls.front().overlaps.empty());
  REQUIRE(overlap_facts.calls.front().overlaps.front().writable_conflict);
  REQUIRE(!mpf::detail::mir::verify_alias_effects(corrupted, overlap_facts, "overlap").empty());
}

TEST_CASE("backends create isolated semantic pipelines and strongly typed LIR artifacts") {
  static_assert(
      !std::is_same_v<mpf::detail::javascript::lir::Program, mpf::detail::cpp::lir::Program>);
  static_assert(
      !std::is_same_v<mpf::detail::javascript::lir::Expression, mpf::detail::cpp::lir::Expression>);

  auto lowered = lower_python("print(abs(-2))\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  const auto alias_effects = mpf::detail::mir::analyze_alias_effects(mir.program);
  mpf::TranspileOptions options;
  auto javascript = mpf::detail::javascript::lower(mir.program, alias_effects, options);
  auto cpp = mpf::detail::cpp::lower(mir.program, alias_effects, options);
  REQUIRE(javascript.diagnostics.empty());
  REQUIRE(cpp.diagnostics.empty());
  REQUIRE(javascript.artifact != nullptr);
  REQUIRE(cpp.artifact != nullptr);
  REQUIRE(javascript.artifact->target() == mpf::TargetLanguage::javascript);
  REQUIRE(cpp.artifact->target() == mpf::TargetLanguage::cpp);
  REQUIRE(mpf::detail::javascript::verify_artifact(*javascript.artifact).empty());
  REQUIRE(mpf::detail::cpp::verify_artifact(*cpp.artifact).empty());
  auto stale_effects = alias_effects;
  ++stale_effects.mir_revision;
  REQUIRE(!mpf::detail::javascript::lower(mir.program, stale_effects, options).diagnostics.empty());
  const auto javascript_dump = javascript.artifact->debug_dump();
  const auto cpp_dump = cpp.artifact->debug_dump();
  REQUIRE(javascript_dump.find("javascript-semantic-lir-v12") != std::string::npos);
  REQUIRE(javascript_dump.find("expr %l") != std::string::npos);
  REQUIRE(cpp_dump.find("cpp-semantic-lir-v12") != std::string::npos);
  REQUIRE(cpp_dump.find("function-order") != std::string::npos);
  REQUIRE(javascript_dump == read_golden("lir/javascript-basic.lir"));
  REQUIRE(cpp_dump == read_golden("lir/cpp-basic.lir"));
  REQUIRE(mpf::detail::legalization_table_complete(mpf::detail::javascript::legalization_table()));
  REQUIRE(mpf::detail::legalization_table_complete(mpf::detail::cpp::legalization_table()));

  auto explicit_rejection = mpf::detail::javascript::legalization_table();
  explicit_rejection[static_cast<std::size_t>(mpf::detail::mir::Opcode::call)] =
      mpf::detail::LegalizationAction::unavailable;
  REQUIRE(mpf::detail::legalization_table_complete(explicit_rejection));
  REQUIRE(!mpf::detail::validate_legalizations(mir.program, explicit_rejection, "negative-target")
               .empty());
}

TEST_CASE("frontend and backend extension conformance harnesses are reusable") {
  mpf::detail::SourceManager sources;
  const auto source_id = sources.add("print(abs(-2))\n", "conformance.py");
  const auto& source = sources.source(source_id);
  const auto& frontend = mpf::detail::python_frontend();
  REQUIRE(mpf::detail::run_frontend_conformance(frontend, source).empty());

  const auto typescript_id =
      sources.add("const answer: number = 42;\nconsole.log(answer);\n", "conformance.ts");
  REQUIRE(mpf::detail::run_frontend_conformance(mpf::detail::typescript_frontend(),
                                                sources.source(typescript_id))
              .empty());

  auto parsed = mpf::detail::parse_with_frontend(frontend, source);
  REQUIRE(parsed.diagnostics.empty());
  REQUIRE(frontend.verify(parsed.ast).empty());
  auto hir = frontend.lower(std::move(parsed.ast));
  REQUIRE(hir.diagnostics.empty());
  auto analysis = mpf::detail::analyze_program(hir.program, std::move(hir.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(hir.program), std::move(analysis.semantics),
                                              analysis.names);
  REQUIRE(mir.diagnostics.empty());
  const auto alias_effects = mpf::detail::mir::analyze_alias_effects(mir.program);
  const auto* javascript = mpf::detail::find_backend(mpf::TargetLanguage::javascript);
  const auto* cpp = mpf::detail::find_backend(mpf::TargetLanguage::cpp);
  REQUIRE(javascript != nullptr);
  REQUIRE(cpp != nullptr);
  REQUIRE(mpf::detail::run_backend_conformance(*javascript, mir.program, alias_effects).empty());
  REQUIRE(mpf::detail::run_backend_conformance(*cpp, mir.program, alias_effects).empty());
}

TEST_CASE("target LIR verifiers reject missing identities and cross-target artifacts") {
  mpf::detail::javascript::lir::Program javascript;
  javascript.node_count = 1;
  REQUIRE(!mpf::detail::javascript::verify_artifact(javascript).empty());
  REQUIRE(!mpf::detail::cpp::verify_artifact(javascript).empty());
}

TEST_CASE("target identifier inventory preserves SymbolId identity and scoped spelling reuse") {
  mpf::detail::IdentifierInventory inventory;
  mpf::detail::add_identifier(inventory, mpf::detail::SymbolId{1}, "class");
  mpf::detail::add_identifier(inventory, mpf::detail::SymbolId{2}, "class");
  mpf::detail::add_identifier(inventory, {}, "mpf_class");
  const auto plan = mpf::detail::allocate_identifiers(mpf::TargetLanguage::javascript, inventory);
  REQUIRE(mpf::detail::identifier_plan_complete(plan, inventory));
  REQUIRE(plan.symbols.at(mpf::detail::SymbolId{1}) == plan.symbols.at(mpf::detail::SymbolId{2}));
  REQUIRE(plan.symbols.at(mpf::detail::SymbolId{1}) != plan.names.at("mpf_class"));

  mpf::detail::IdentifierMangler mangler(plan);
  REQUIRE(mangler.name(mpf::detail::SymbolId{1}, "class") ==
          mangler.name(mpf::detail::SymbolId{2}, "class"));

  auto cpp_inventory = inventory;
  cpp_inventory.require_unique_symbol_names = true;
  const auto cpp_plan = mpf::detail::allocate_identifiers(mpf::TargetLanguage::cpp, cpp_inventory);
  REQUIRE(mpf::detail::identifier_plan_complete(cpp_plan, cpp_inventory));
  REQUIRE(cpp_plan.symbols.at(mpf::detail::SymbolId{1}) !=
          cpp_plan.symbols.at(mpf::detail::SymbolId{2}));
  REQUIRE(cpp_plan.symbols.at(mpf::detail::SymbolId{1}) != cpp_plan.names.at("mpf_class"));
  REQUIRE(cpp_plan.symbols.at(mpf::detail::SymbolId{2}) != cpp_plan.names.at("mpf_class"));

  auto invalid = inventory;
  mpf::detail::add_identifier(invalid, mpf::detail::SymbolId{1}, "different");
  REQUIRE(!invalid.valid);
  REQUIRE(!mpf::detail::identifier_plan_complete(plan, invalid));
}

TEST_CASE("target LIR verifiers require scope ABI and dense temporary plans") {
  mpf::detail::javascript::lir::SemanticProgram javascript;
  javascript.node_count = 1;
  mpf::detail::javascript::lir::Statement javascript_range;
  javascript_range.id = mpf::detail::LirNodeId{1};
  javascript_range.origin = mpf::detail::HirNodeId{1};
  javascript_range.kind = mpf::detail::StatementKind::range_loop;
  javascript_range.name = "index";
  javascript_range.retain_last_loop_value = false;
  javascript.statements.push_back(std::move(javascript_range));
  javascript.identifiers = mpf::detail::allocate_identifiers(
      mpf::TargetLanguage::javascript, mpf::detail::collect_identifier_inventory(javascript));
  REQUIRE(!mpf::detail::javascript::verify_semantic_lir(javascript).empty());
  mpf::detail::javascript::plan_lir_resources(javascript, mpf::TranspileOptions{});
  mpf::detail::javascript::plan_lir_representation(javascript);
  REQUIRE(mpf::detail::javascript::verify_semantic_lir(javascript).empty());
  javascript.program_scope.declarations.clear();
  REQUIRE(!mpf::detail::javascript::verify_semantic_lir(javascript).empty());
  javascript.program_scope.declarations = {{{}, "index"}};
  javascript.temporaries.slots.front().name = javascript.identifiers.names.at("index");
  REQUIRE(!mpf::detail::javascript::verify_semantic_lir(javascript).empty());

  mpf::detail::javascript::lir::SemanticProgram javascript_function;
  javascript_function.node_count = 1;
  mpf::detail::javascript::lir::Statement javascript_abi;
  javascript_abi.id = mpf::detail::LirNodeId{1};
  javascript_abi.origin = mpf::detail::HirNodeId{1};
  javascript_abi.kind = mpf::detail::StatementKind::function;
  javascript_abi.name = "write";
  javascript_abi.parameters = {"value"};
  javascript_abi.parameter_symbols = {{}};
  javascript_abi.parameter_intents = {mpf::detail::ParameterIntent::out};
  javascript_function.statements.push_back(std::move(javascript_abi));
  javascript_function.identifiers = mpf::detail::allocate_identifiers(
      mpf::TargetLanguage::javascript,
      mpf::detail::collect_identifier_inventory(javascript_function));
  mpf::detail::javascript::plan_lir_resources(javascript_function, mpf::TranspileOptions{});
  mpf::detail::javascript::plan_lir_representation(javascript_function);
  REQUIRE(mpf::detail::javascript::verify_semantic_lir(javascript_function).empty());
  javascript_function.statements.front().function_abi.parameters.front() =
      mpf::detail::javascript::lir::ParameterPassing::value;
  REQUIRE(!mpf::detail::javascript::verify_semantic_lir(javascript_function).empty());
  javascript_function.statements.front().function_abi.parameters.front() =
      mpf::detail::javascript::lir::ParameterPassing::reference_box;
  javascript_function.module.body_order.clear();
  REQUIRE(!mpf::detail::javascript::verify_semantic_lir(javascript_function).empty());
  javascript_function.module.body_order = {0};
  REQUIRE(mpf::detail::javascript::verify_semantic_lir(javascript_function).empty());

  mpf::detail::cpp::lir::SemanticProgram cpp;
  cpp.node_count = 1;
  mpf::detail::cpp::lir::Statement cpp_function;
  cpp_function.id = mpf::detail::LirNodeId{1};
  cpp_function.origin = mpf::detail::HirNodeId{1};
  cpp_function.kind = mpf::detail::StatementKind::function;
  cpp_function.name = "compute";
  cpp.statements.push_back(std::move(cpp_function));
  cpp.function_graph.dependencies.resize(1);
  cpp.function_graph.recursive.resize(1, false);
  cpp.function_graph.definition_order.push_back(0);
  cpp.identifiers = mpf::detail::allocate_identifiers(
      mpf::TargetLanguage::cpp, mpf::detail::collect_identifier_inventory(cpp));
  REQUIRE(!mpf::detail::cpp::verify_semantic_lir(cpp).empty());
  mpf::detail::cpp::plan_lir_resources(cpp, mpf::TranspileOptions{});
  mpf::detail::cpp::plan_lir_representation(cpp);
  REQUIRE(mpf::detail::cpp::verify_semantic_lir(cpp).empty());
  mpf::detail::cpp::lir::DeclarationPlan unexpected;
  unexpected.name = "unexpected";
  cpp.program_scope.declarations.push_back(std::move(unexpected));
  REQUIRE(!mpf::detail::cpp::verify_semantic_lir(cpp).empty());
  cpp.program_scope.declarations.clear();
  cpp.statements.front().function_abi.return_type = "void";
  REQUIRE(!mpf::detail::cpp::verify_semantic_lir(cpp).empty());
  cpp.statements.front().function_abi.return_type = "auto";
  cpp.translation_unit.definitions.clear();
  REQUIRE(!mpf::detail::cpp::verify_semantic_lir(cpp).empty());
}

TEST_CASE("target LIR owns module and translation-unit topology") {
  mpf::detail::javascript::lir::SemanticProgram javascript;
  javascript.source_language = mpf::SourceLanguage::python;
  javascript.node_count = 1;
  javascript.emission.module =
      mpf::detail::javascript::lir::EmissionPlan::ModuleFormat::strict_script;
  javascript.runtime.require(mpf::detail::javascript::lir::RuntimeFeature::dynamic_values);
  javascript.runtime.require(mpf::detail::javascript::lir::RuntimeFeature::arrays);
  mpf::detail::javascript::lir::Statement javascript_statement;
  javascript_statement.id = mpf::detail::LirNodeId{1};
  javascript_statement.origin = mpf::detail::HirNodeId{1};
  javascript.statements.push_back(std::move(javascript_statement));
  javascript.identifiers = mpf::detail::allocate_identifiers(
      mpf::TargetLanguage::javascript, mpf::detail::collect_identifier_inventory(javascript));
  mpf::TranspileOptions options;
  options.emit_source_banner = false;
  mpf::detail::javascript::plan_lir_resources(javascript, options);
  mpf::detail::javascript::plan_lir_representation(javascript);
  REQUIRE(!javascript.module.emit_banner);
  REQUIRE(javascript.module.banner.empty());
  REQUIRE((javascript.module.directives == std::vector<std::string>{"\"use strict\";"}));
  REQUIRE((javascript.module.runtime_fragments ==
           std::vector<mpf::detail::javascript::lir::RuntimeFragment>{
               mpf::detail::javascript::lir::RuntimeFragment::dynamic_values,
               mpf::detail::javascript::lir::RuntimeFragment::arrays}));
  REQUIRE((javascript.module.body_order == std::vector<std::size_t>{0}));
  REQUIRE(mpf::detail::javascript::verify_semantic_lir(javascript).empty());
  javascript.emission.padded_character_selection = true;
  REQUIRE(!mpf::detail::javascript::verify_semantic_lir(javascript).empty());
  javascript.emission.padded_character_selection = false;
  std::reverse(javascript.module.runtime_fragments.begin(),
               javascript.module.runtime_fragments.end());
  REQUIRE(!mpf::detail::javascript::verify_semantic_lir(javascript).empty());

  mpf::detail::cpp::lir::SemanticProgram cpp;
  cpp.source_language = mpf::SourceLanguage::python;
  cpp.node_count = 1;
  mpf::detail::cpp::lir::Statement cpp_statement;
  cpp_statement.id = mpf::detail::LirNodeId{1};
  cpp_statement.origin = mpf::detail::HirNodeId{1};
  cpp.statements.push_back(std::move(cpp_statement));
  cpp.identifiers = mpf::detail::allocate_identifiers(
      mpf::TargetLanguage::cpp, mpf::detail::collect_identifier_inventory(cpp));
  mpf::detail::cpp::plan_lir_resources(cpp, options);
  mpf::detail::cpp::plan_lir_representation(cpp);
  REQUIRE(!cpp.translation_unit.emit_banner);
  REQUIRE(cpp.translation_unit.banner.empty());
  REQUIRE((cpp.translation_unit.runtime_fragments ==
           std::vector<mpf::detail::cpp::lir::RuntimeFragment>{
               mpf::detail::cpp::lir::RuntimeFragment::core}));
  REQUIRE(cpp.translation_unit.definitions.empty());
  REQUIRE((cpp.translation_unit.entry_statements == std::vector<std::size_t>{0}));
  REQUIRE(cpp.translation_unit.emit_entry_function);
  REQUIRE(cpp.translation_unit.emit_main);
  REQUIRE(mpf::detail::cpp::verify_semantic_lir(cpp).empty());
  cpp.emission.padded_character_selection = true;
  REQUIRE(!mpf::detail::cpp::verify_semantic_lir(cpp).empty());
  cpp.emission.padded_character_selection = false;
  std::swap(cpp.translation_unit.standard_headers.front(),
            cpp.translation_unit.standard_headers.back());
  REQUIRE(!mpf::detail::cpp::verify_semantic_lir(cpp).empty());
}

TEST_CASE("target LIR expression plans own operators calls and concrete representation") {
  mpf::detail::javascript::lir::SemanticProgram javascript;
  javascript.emission.dynamic_truthiness = true;
  javascript.emission.operand_logical_result = true;
  javascript.emission.structural_equality = true;
  mpf::detail::javascript::lir::Statement javascript_statement;
  javascript_statement.expression.kind = mpf::detail::ExpressionKind::call;
  javascript_statement.expression.multi_output_call = true;
  javascript_statement.expression.requested_outputs = 1;
  javascript_statement.expression.argument_transfers = {
      mpf::detail::ArgumentTransfer::mutable_borrow_out};
  mpf::detail::javascript::lir::Expression javascript_callee;
  javascript_callee.kind = mpf::detail::ExpressionKind::identifier;
  javascript_callee.value = "update";
  mpf::detail::javascript::lir::Expression javascript_argument;
  javascript_argument.kind = mpf::detail::ExpressionKind::identifier;
  javascript_argument.value = "value";
  javascript_argument.inferred_type = mpf::detail::ValueType::integer;
  javascript_statement.expression.children = {std::move(javascript_callee),
                                              std::move(javascript_argument)};
  javascript.statements.push_back(std::move(javascript_statement));
  mpf::detail::javascript::lir::Statement javascript_index_statement;
  javascript_index_statement.expression.kind = mpf::detail::ExpressionKind::index;
  mpf::detail::javascript::lir::Expression javascript_container;
  javascript_container.kind = mpf::detail::ExpressionKind::identifier;
  javascript_container.value = "items";
  mpf::detail::javascript::lir::Expression javascript_slice;
  javascript_slice.kind = mpf::detail::ExpressionKind::slice;
  javascript_slice.children.resize(3);
  mpf::detail::javascript::lir::Expression javascript_selector;
  javascript_selector.kind = mpf::detail::ExpressionKind::number_literal;
  javascript_selector.value = "1";
  javascript_index_statement.expression.children = {
      std::move(javascript_container), std::move(javascript_slice), std::move(javascript_selector)};
  javascript.statements.push_back(std::move(javascript_index_statement));
  mpf::detail::javascript::lir::Statement javascript_comparison_statement;
  javascript_comparison_statement.expression.kind = mpf::detail::ExpressionKind::binary;
  javascript_comparison_statement.expression.comparison =
      mpf::detail::ComparisonOperator::not_contains;
  javascript_comparison_statement.expression.children.resize(2);
  javascript_comparison_statement.expression.children[0].kind =
      mpf::detail::ExpressionKind::number_literal;
  javascript_comparison_statement.expression.children[1].kind = mpf::detail::ExpressionKind::list;
  javascript.statements.push_back(std::move(javascript_comparison_statement));
  mpf::detail::javascript::plan_lir_representation(javascript);
  const auto& javascript_plan = javascript.statements.front().expression.plan;
  REQUIRE(javascript_plan.form == mpf::detail::javascript::lir::ExpressionForm::call);
  REQUIRE(javascript_plan.call == mpf::detail::javascript::lir::CallForm::direct);
  REQUIRE(javascript_plan.call_value == mpf::detail::javascript::lir::CallValueForm::first_result);
  REQUIRE(javascript_plan.evaluation ==
          mpf::detail::javascript::lir::EvaluationForm::writable_call_arrow_iife);
  REQUIRE(javascript_plan.call_arguments.front().form ==
          mpf::detail::javascript::lir::CallArgumentForm::reference_box_uninitialized);
  REQUIRE(javascript_plan.call_arguments.front().writeback ==
          mpf::detail::javascript::lir::WritebackForm::direct);
  const auto& javascript_index_plan = javascript.statements[1].expression.plan;
  REQUIRE(javascript_index_plan.index == mpf::detail::javascript::lir::IndexForm::section);
  REQUIRE((javascript_index_plan.selector_slices == std::vector<bool>{true, false}));
  const auto& javascript_comparison_plan = javascript.statements[2].expression.plan;
  REQUIRE(javascript_comparison_plan.form ==
          mpf::detail::javascript::lir::ExpressionForm::binary_comparison);
  REQUIRE(javascript_comparison_plan.comparisons.front().form ==
          mpf::detail::javascript::lir::ComparisonForm::not_membership);
  std::vector<mpf::Diagnostic> diagnostics;
  mpf::detail::javascript::verify_lir_representation(javascript, diagnostics);
  REQUIRE(diagnostics.empty());
  javascript.statements[2].expression.plan.comparisons.front().form =
      mpf::detail::javascript::lir::ComparisonForm::identity;
  mpf::detail::javascript::verify_lir_representation(javascript, diagnostics);
  REQUIRE(!diagnostics.empty());
  javascript.statements[2].expression.plan.comparisons.front().form =
      mpf::detail::javascript::lir::ComparisonForm::not_membership;
  diagnostics.clear();
  javascript.statements.front().expression.plan.call = mpf::detail::javascript::lir::CallForm::sum;
  mpf::detail::javascript::verify_lir_representation(javascript, diagnostics);
  REQUIRE(!diagnostics.empty());

  mpf::detail::cpp::lir::SemanticProgram cpp;
  cpp.emission.dynamic_truthiness = true;
  cpp.emission.real_division = true;
  mpf::detail::cpp::lir::Statement cpp_statement;
  cpp_statement.expression.kind = mpf::detail::ExpressionKind::list;
  cpp_statement.expression.element_type = mpf::detail::ValueType::real;
  cpp_statement.expression.shape = {2};
  mpf::detail::cpp::lir::Expression integer_child;
  integer_child.kind = mpf::detail::ExpressionKind::number_literal;
  integer_child.value = "1";
  integer_child.inferred_type = mpf::detail::ValueType::integer;
  mpf::detail::cpp::lir::Expression real_child;
  real_child.kind = mpf::detail::ExpressionKind::number_literal;
  real_child.value = "2.5";
  real_child.inferred_type = mpf::detail::ValueType::real;
  cpp_statement.expression.children = {std::move(integer_child), std::move(real_child)};
  cpp.statements.push_back(std::move(cpp_statement));
  mpf::detail::cpp::lir::Statement cpp_index_statement;
  cpp_index_statement.expression.kind = mpf::detail::ExpressionKind::index;
  mpf::detail::cpp::lir::Expression cpp_container;
  cpp_container.kind = mpf::detail::ExpressionKind::identifier;
  cpp_container.value = "tensor";
  mpf::detail::cpp::lir::Expression cpp_slice;
  cpp_slice.kind = mpf::detail::ExpressionKind::slice;
  cpp_slice.children.resize(3);
  mpf::detail::cpp::lir::Expression cpp_selector;
  cpp_selector.kind = mpf::detail::ExpressionKind::number_literal;
  cpp_selector.value = "1";
  cpp_index_statement.expression.children = {std::move(cpp_container), std::move(cpp_slice),
                                             cpp_selector, std::move(cpp_selector)};
  cpp.statements.push_back(std::move(cpp_index_statement));
  mpf::detail::cpp::lir::Statement cpp_call_statement;
  cpp_call_statement.expression.kind = mpf::detail::ExpressionKind::call;
  cpp_call_statement.expression.procedure_has_result = true;
  cpp_call_statement.expression.argument_transfers = {mpf::detail::ArgumentTransfer::copy_in_out};
  mpf::detail::cpp::lir::Expression cpp_callee;
  cpp_callee.kind = mpf::detail::ExpressionKind::identifier;
  cpp_callee.value = "update";
  mpf::detail::cpp::lir::Expression cpp_section;
  cpp_section.kind = mpf::detail::ExpressionKind::index;
  mpf::detail::cpp::lir::Expression cpp_section_base;
  cpp_section_base.kind = mpf::detail::ExpressionKind::identifier;
  cpp_section_base.value = "items";
  mpf::detail::cpp::lir::Expression cpp_section_slice;
  cpp_section_slice.kind = mpf::detail::ExpressionKind::slice;
  cpp_section_slice.children.resize(3);
  cpp_section.children = {std::move(cpp_section_base), std::move(cpp_section_slice)};
  cpp_call_statement.expression.children = {std::move(cpp_callee), std::move(cpp_section)};
  cpp.statements.push_back(std::move(cpp_call_statement));
  mpf::detail::cpp::lir::Statement cpp_comparison_statement;
  cpp_comparison_statement.expression.kind = mpf::detail::ExpressionKind::binary;
  cpp_comparison_statement.expression.comparison = mpf::detail::ComparisonOperator::identity;
  cpp_comparison_statement.expression.children.resize(2);
  cpp_comparison_statement.expression.children[0].kind = mpf::detail::ExpressionKind::null_literal;
  cpp_comparison_statement.expression.children[1].kind = mpf::detail::ExpressionKind::null_literal;
  cpp.statements.push_back(std::move(cpp_comparison_statement));
  mpf::detail::cpp::plan_lir_representation(cpp);
  const auto& cpp_plan = cpp.statements.front().expression.plan;
  REQUIRE(cpp_plan.form == mpf::detail::cpp::lir::ExpressionForm::list);
  REQUIRE(cpp_plan.concrete_type == "std::vector<double>");
  REQUIRE((cpp_plan.widen_children == std::vector<bool>{true, true}));
  const auto& cpp_index_plan = cpp.statements[1].expression.plan;
  REQUIRE(cpp_index_plan.index == mpf::detail::cpp::lir::IndexForm::section_nd);
  REQUIRE((cpp_index_plan.selector_slices == std::vector<bool>{true, false, false}));
  const auto& cpp_call_plan = cpp.statements[2].expression.plan;
  REQUIRE(cpp_call_plan.evaluation ==
          mpf::detail::cpp::lir::EvaluationForm::copy_call_reference_lambda_iife);
  REQUIRE(cpp_call_plan.call_outcome == mpf::detail::cpp::lir::CallOutcomeForm::value);
  REQUIRE(cpp_call_plan.call_arguments.front().form ==
          mpf::detail::cpp::lir::CallArgumentForm::copy_section);
  REQUIRE(cpp_call_plan.call_arguments.front().writeback ==
          mpf::detail::cpp::lir::WritebackForm::section);
  const auto& cpp_comparison_plan = cpp.statements[3].expression.plan;
  REQUIRE(cpp_comparison_plan.form == mpf::detail::cpp::lir::ExpressionForm::binary_comparison);
  REQUIRE(cpp_comparison_plan.evaluation ==
          mpf::detail::cpp::lir::EvaluationForm::binary_comparison_reference_lambda_iife);
  REQUIRE(cpp_comparison_plan.comparisons.front().form ==
          mpf::detail::cpp::lir::ComparisonForm::identity);
  diagnostics.clear();
  mpf::detail::cpp::verify_lir_representation(cpp, diagnostics);
  REQUIRE(diagnostics.empty());
  cpp.statements[3].expression.plan.comparisons.front().form =
      mpf::detail::cpp::lir::ComparisonForm::membership;
  mpf::detail::cpp::verify_lir_representation(cpp, diagnostics);
  REQUIRE(!diagnostics.empty());
  cpp.statements[3].expression.plan.comparisons.front().form =
      mpf::detail::cpp::lir::ComparisonForm::identity;
  diagnostics.clear();
  cpp.statements.front().expression.plan.concrete_type = "std::vector<float>";
  mpf::detail::cpp::verify_lir_representation(cpp, diagnostics);
  REQUIRE(!diagnostics.empty());
}

TEST_CASE("target LIR statement plans own control assignment and parameter access") {
  mpf::detail::javascript::lir::SemanticProgram javascript;
  javascript.emission.dynamic_truthiness = true;
  javascript.emission.resizable_sections = true;
  javascript.emission.emit_parameter_defaults = true;

  mpf::detail::javascript::lir::Statement javascript_function;
  javascript_function.kind = mpf::detail::StatementKind::function;
  javascript_function.name = "update";
  javascript_function.parameters = {"value"};
  javascript_function.function_abi.valid = true;
  javascript_function.function_abi.parameters = {
      mpf::detail::javascript::lir::ParameterPassing::reference_box};
  mpf::detail::javascript::lir::Statement javascript_assignment;
  javascript_assignment.kind = mpf::detail::StatementKind::assignment;
  javascript_assignment.name = "value";
  javascript_assignment.expression.kind = mpf::detail::ExpressionKind::identifier;
  javascript_assignment.expression.value = "value";
  javascript_function.body.push_back(std::move(javascript_assignment));
  javascript.statements.push_back(std::move(javascript_function));

  mpf::detail::javascript::lir::Statement javascript_array;
  javascript_array.kind = mpf::detail::StatementKind::declaration;
  javascript_array.name = "cube";
  javascript_array.declared_type = mpf::detail::ValueType::list;
  javascript_array.element_type = mpf::detail::ValueType::boolean;
  javascript_array.shape = {2, 3, 4};
  javascript.statements.push_back(std::move(javascript_array));

  mpf::detail::javascript::plan_lir_representation(javascript);
  const auto& javascript_body = javascript.statements.front().body.front();
  REQUIRE(javascript.statements.front().plan.form ==
          mpf::detail::javascript::lir::StatementForm::function);
  REQUIRE(javascript_body.plan.form == mpf::detail::javascript::lir::StatementForm::assignment);
  REQUIRE(javascript_body.plan.target_access ==
          mpf::detail::javascript::lir::VariableAccess::reference_box_value);
  REQUIRE(javascript_body.expression.plan.variable_access ==
          mpf::detail::javascript::lir::VariableAccess::reference_box_value);
  REQUIRE(javascript.statements[1].plan.form ==
          mpf::detail::javascript::lir::StatementForm::declaration_array);
  REQUIRE((javascript.statements[1].plan.array_shape == std::vector<std::size_t>{2, 3, 4}));
  REQUIRE(javascript.statements[1].plan.array_default == "false");
  std::vector<mpf::Diagnostic> diagnostics;
  mpf::detail::javascript::verify_lir_representation(javascript, diagnostics);
  REQUIRE(diagnostics.empty());
  javascript.statements.front().body.front().plan.target_access =
      mpf::detail::javascript::lir::VariableAccess::direct;
  mpf::detail::javascript::verify_lir_representation(javascript, diagnostics);
  REQUIRE(!diagnostics.empty());

  mpf::detail::cpp::lir::SemanticProgram cpp;
  cpp.emission.dynamic_truthiness = true;
  mpf::detail::cpp::lir::Statement cpp_function;
  cpp_function.kind = mpf::detail::StatementKind::function;
  cpp_function.name = "maybe_update";
  cpp_function.parameters = {"value"};
  cpp_function.function_abi.valid = true;
  mpf::detail::cpp::lir::ParameterAbi optional_parameter;
  optional_parameter.passing = mpf::detail::cpp::lir::ParameterPassing::optional_reference;
  optional_parameter.concrete_type = "std::int64_t";
  cpp_function.function_abi.parameters.push_back(std::move(optional_parameter));
  mpf::detail::cpp::lir::Statement cpp_conditional;
  cpp_conditional.kind = mpf::detail::StatementKind::if_statement;
  cpp_conditional.expression.kind = mpf::detail::ExpressionKind::identifier;
  cpp_conditional.expression.value = "value";
  mpf::detail::cpp::lir::Statement cpp_assignment;
  cpp_assignment.kind = mpf::detail::StatementKind::assignment;
  cpp_assignment.name = "value";
  cpp_assignment.expression.kind = mpf::detail::ExpressionKind::identifier;
  cpp_assignment.expression.value = "value";
  cpp_conditional.body.push_back(std::move(cpp_assignment));
  cpp_function.body.push_back(std::move(cpp_conditional));
  cpp.statements.push_back(std::move(cpp_function));

  mpf::detail::cpp::plan_lir_representation(cpp);
  const auto& cpp_if = cpp.statements.front().body.front();
  REQUIRE(cpp_if.plan.form == mpf::detail::cpp::lir::StatementForm::conditional);
  REQUIRE(cpp_if.plan.condition == mpf::detail::cpp::lir::ConditionForm::runtime_truthy);
  REQUIRE(cpp_if.expression.plan.variable_access ==
          mpf::detail::cpp::lir::VariableAccess::optional_value);
  REQUIRE(cpp_if.body.front().plan.target_access ==
          mpf::detail::cpp::lir::VariableAccess::optional_value);
  diagnostics.clear();
  mpf::detail::cpp::verify_lir_representation(cpp, diagnostics);
  REQUIRE(diagnostics.empty());
  cpp.statements.front().body.front().plan.condition = mpf::detail::cpp::lir::ConditionForm::direct;
  mpf::detail::cpp::verify_lir_representation(cpp, diagnostics);
  REQUIRE(!diagnostics.empty());
}

TEST_CASE("target LIR owns dense source segments and closure evaluation plans") {
  mpf::detail::javascript::lir::SemanticProgram javascript;
  javascript.node_count = 4;
  javascript.emission.operand_logical_result = true;
  mpf::detail::javascript::lir::Statement javascript_statement;
  javascript_statement.id = mpf::detail::LirNodeId{1};
  javascript_statement.origin = mpf::detail::HirNodeId{10};
  javascript_statement.line = 7;
  javascript_statement.expression.id = mpf::detail::LirNodeId{2};
  javascript_statement.expression.origin = mpf::detail::HirNodeId{11};
  javascript_statement.expression.location = {7, 5};
  javascript_statement.expression.kind = mpf::detail::ExpressionKind::binary;
  javascript_statement.expression.value = "&&";
  javascript_statement.expression.operation = mpf::detail::BinaryOperator::logical_and;
  mpf::detail::javascript::lir::Expression javascript_left;
  javascript_left.id = mpf::detail::LirNodeId{3};
  javascript_left.origin = mpf::detail::HirNodeId{12};
  javascript_left.location = {7, 5};
  javascript_left.kind = mpf::detail::ExpressionKind::boolean_literal;
  javascript_left.value = "true";
  mpf::detail::javascript::lir::Expression javascript_right;
  javascript_right.id = mpf::detail::LirNodeId{4};
  javascript_right.origin = mpf::detail::HirNodeId{13};
  javascript_right.location = {7, 13};
  javascript_right.kind = mpf::detail::ExpressionKind::boolean_literal;
  javascript_right.value = "false";
  javascript_statement.expression.children = {std::move(javascript_left),
                                              std::move(javascript_right)};
  javascript.statements.push_back(std::move(javascript_statement));
  mpf::detail::javascript::plan_lir_representation(javascript);
  REQUIRE(javascript.statements.front().expression.plan.evaluation ==
          mpf::detail::javascript::lir::EvaluationForm::lazy_arrow_thunks);
  REQUIRE(javascript.source_segments.valid);
  REQUIRE(javascript.source_segments.nodes.size() == 5);
  REQUIRE(javascript.source_segments.find(mpf::detail::LirNodeId{1})->source.line == 7);
  REQUIRE(javascript.source_segments.find(mpf::detail::LirNodeId{4})->source.column == 13);
  std::vector<mpf::Diagnostic> diagnostics;
  mpf::detail::javascript::verify_lir_representation(javascript, diagnostics);
  REQUIRE(diagnostics.empty());
  javascript.source_segments.nodes[4].source.column = 12;
  mpf::detail::javascript::verify_lir_representation(javascript, diagnostics);
  REQUIRE(!diagnostics.empty());

  mpf::detail::cpp::lir::SemanticProgram cpp;
  cpp.node_count = 2;
  mpf::detail::cpp::lir::Statement cpp_statement;
  cpp_statement.id = mpf::detail::LirNodeId{1};
  cpp_statement.origin = mpf::detail::HirNodeId{20};
  cpp_statement.line = 9;
  cpp_statement.expression.id = mpf::detail::LirNodeId{2};
  cpp_statement.expression.origin = mpf::detail::HirNodeId{21};
  cpp_statement.expression.location = {9, 3};
  cpp_statement.expression.kind = mpf::detail::ExpressionKind::number_literal;
  cpp_statement.expression.value = "42";
  cpp.statements.push_back(std::move(cpp_statement));
  mpf::detail::cpp::plan_lir_representation(cpp);
  REQUIRE(cpp.source_segments.valid);
  REQUIRE(cpp.source_segments.find(mpf::detail::LirNodeId{2})->origin ==
          mpf::detail::HirNodeId{21});
  diagnostics.clear();
  mpf::detail::cpp::verify_lir_representation(cpp, diagnostics);
  REQUIRE(diagnostics.empty());
  cpp.source_segments.valid = false;
  mpf::detail::cpp::verify_lir_representation(cpp, diagnostics);
  REQUIRE(!diagnostics.empty());
}

TEST_CASE("target LIR scope plans own declarations types and probes") {
  auto lowered = lower_python(
      "value = 40\n"
      "items = [1, 2]\n"
      "pair = (3, 4)\n"
      "def increment(input):\n"
      "    local = input + 1\n"
      "    result = local\n"
      "    return result\n"
      "print(increment(value))\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  const auto effects = mpf::detail::mir::analyze_alias_effects(mir.program);
  const auto javascript =
      mpf::detail::javascript::lower(mir.program, effects, mpf::TranspileOptions{});
  const auto cpp = mpf::detail::cpp::lower(mir.program, effects, mpf::TranspileOptions{});
  REQUIRE(javascript.diagnostics.empty());
  REQUIRE(cpp.diagnostics.empty());
  const auto javascript_dump = javascript.artifact->debug_dump();
  REQUIRE(javascript_dump.find("program-scope [@s") != std::string::npos);
  REQUIRE(javascript_dump.find(":\"items\"") != std::string::npos);
  REQUIRE(javascript_dump.find(":\"pair\"") != std::string::npos);
  REQUIRE(javascript_dump.find("scope [@s") != std::string::npos);
  REQUIRE(javascript_dump.find(":\"local\"") != std::string::npos);
  REQUIRE(javascript_dump.find(":\"result\"") != std::string::npos);
  REQUIRE(javascript_dump.find("module banner=1 directives [] runtime [0] body [0,1,2,3,4]") !=
          std::string::npos);
  const auto cpp_dump = cpp.artifact->debug_dump();
  REQUIRE(cpp_dump.find("program-scope\n  declaration @s") != std::string::npos);
  REQUIRE(cpp_dump.find("\"items\" type-kind 0 type "
                        "\"std::vector<std::int64_t>\"") != std::string::npos);
  REQUIRE(cpp_dump.find("\"pair\" type-kind 1 type \"\" probe %l") != std::string::npos);
  REQUIRE(cpp_dump.find("function-scope %l") != std::string::npos);
  REQUIRE(cpp_dump.find("\"local\" type-kind 0 type \"std::int64_t\"") != std::string::npos);
  REQUIRE(cpp_dump.find("translation-unit banner=1 runtime-namespace \"mpf_runtime\"") !=
          std::string::npos);
  REQUIRE(cpp_dump.find("runtime [0,1] forward [] definitions [4] entry [0,1,2,3]") !=
          std::string::npos);
}

TEST_CASE("MIR verifier rejects ownership control-flow and dominance corruption") {
  auto lowered = lower_python(
      "def choose(value):\n"
      "    if value > 0:\n"
      "        return value\n"
      "    return 0\n"
      "print(choose(1))\n");
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());
  auto lowered_mir = mpf::detail::mir::lower_from_hir(
      std::move(lowered.program), std::move(analysis.semantics), analysis.names);
  REQUIRE(lowered_mir.diagnostics.empty());

  auto cyclic_expression = lowered_mir.program;
  const auto cyclic = std::find_if(
      cyclic_expression.expressions.begin() + 1, cyclic_expression.expressions.end(),
      [](const mpf::detail::mir::Expression& expression) { return !expression.children.empty(); });
  REQUIRE(cyclic != cyclic_expression.expressions.end());
  cyclic->children.front() = cyclic->id;
  cyclic_expression.instructions[cyclic->instruction.value()].operands.front() = cyclic->value_id;
  REQUIRE(!mpf::detail::mir::verify(cyclic_expression, "negative-expression-cycle").empty());

  auto false_tombstone = lowered_mir.program;
  false_tombstone.expressions[1].retired = true;
  REQUIRE(!mpf::detail::mir::verify(false_tombstone, "negative-live-tombstone").empty());

  auto missing_definition = lowered_mir.program;
  auto instruction = std::find_if(
      missing_definition.instructions.begin() + 1, missing_definition.instructions.end(),
      [](const mpf::detail::mir::Instruction& candidate) { return !candidate.operands.empty(); });
  REQUIRE(instruction != missing_definition.instructions.end());
  instruction->operands.front() = mpf::detail::ValueId{999999};
  REQUIRE(!mpf::detail::mir::verify(missing_definition, "negative-use").empty());

  auto duplicate_owner = lowered_mir.program;
  const auto& function = duplicate_owner.functions[1];
  REQUIRE(!function.blocks.empty());
  auto& block = duplicate_owner.blocks[function.blocks.front().value()];
  REQUIRE(!block.instructions.empty());
  block.instructions.push_back(block.instructions.front());
  REQUIRE(!mpf::detail::mir::verify(duplicate_owner, "negative-owner").empty());

  auto cross_function = lowered_mir.program;
  REQUIRE(cross_function.functions.size() > 2);
  auto& module_exit = cross_function.blocks[cross_function.functions[1].blocks.back().value()];
  module_exit.terminator.kind = mpf::detail::mir::TerminatorKind::branch;
  module_exit.terminator.operands.clear();
  module_exit.terminator.successors = {cross_function.functions[2].entry};
  REQUIRE(!mpf::detail::mir::verify(cross_function, "negative-edge").empty());

  auto lowered_with_phi = lower_python(
      "value = 0\n"
      "if value == 0:\n"
      "    value = 1\n"
      "else:\n"
      "    value = 2\n"
      "print(value)\n");
  auto phi_analysis =
      mpf::detail::analyze_program(lowered_with_phi.program, std::move(lowered_with_phi.semantics));
  REQUIRE(phi_analysis.empty());
  auto bad_edge_arguments =
      mpf::detail::mir::lower_from_hir(std::move(lowered_with_phi.program),
                                       std::move(phi_analysis.semantics), phi_analysis.names)
          .program;
  const auto argument_edge =
      std::find_if(bad_edge_arguments.blocks.begin() + 1, bad_edge_arguments.blocks.end(),
                   [](const mpf::detail::mir::BasicBlock& candidate) {
                     return std::any_of(candidate.terminator.successor_arguments.begin(),
                                        candidate.terminator.successor_arguments.end(),
                                        [](const auto& arguments) { return !arguments.empty(); });
                   });
  REQUIRE(argument_edge != bad_edge_arguments.blocks.end());
  auto& arguments = *std::find_if(argument_edge->terminator.successor_arguments.begin(),
                                  argument_edge->terminator.successor_arguments.end(),
                                  [](const auto& values) { return !values.empty(); });
  arguments.pop_back();
  REQUIRE(!mpf::detail::mir::verify(bad_edge_arguments, "negative-edge-arity").empty());
}
