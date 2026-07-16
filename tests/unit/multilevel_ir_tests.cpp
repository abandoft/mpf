#include <algorithm>
#include <fstream>
#include <sstream>
#include <type_traits>

#include "backends/backend_conformance.hpp"
#include "backends/cpp_lir.hpp"
#include "backends/cpp_lowering.hpp"
#include "backends/javascript_lir.hpp"
#include "backends/javascript_lowering.hpp"
#include "core/backend_registry.hpp"
#include "frontends/frontend_conformance.hpp"
#include "frontends/frontend_registry.hpp"
#include "ir/dump.hpp"
#include "ir/hir.hpp"
#include "ir/mir.hpp"
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
  REQUIRE(frontend.verify(parsed.ast).empty());
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
  auto analysis = mpf::detail::analyze_program(lowered.program);
  REQUIRE(analysis.empty());
  const auto first_hir = mpf::detail::dump_hir(lowered.program);
  const auto second_hir = mpf::detail::dump_hir(lowered.program);
  REQUIRE(first_hir == second_hir);
  REQUIRE(first_hir.find("hir-v1") != std::string::npos);
  REQUIRE(first_hir.find("stmt %h") != std::string::npos);
  const auto first_semantics = mpf::detail::dump_semantics(analysis.semantics);
  REQUIRE(first_semantics == mpf::detail::dump_semantics(analysis.semantics));
  REQUIRE(first_semantics.find("semantic-v1") != std::string::npos);

  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  const auto alias_effects = mpf::detail::mir::analyze_alias_effects(mir.program);
  const auto first_mir = mpf::detail::dump_mir(mir.program, alias_effects);
  REQUIRE(first_mir == mpf::detail::dump_mir(mir.program, alias_effects));
  REQUIRE(first_mir.find("mir-v1") != std::string::npos);
  REQUIRE(first_mir.find("alias-effect-v1") != std::string::npos);
  REQUIRE(first_mir.find("function @f") != std::string::npos);
  REQUIRE(first_mir.find("terminator op") != std::string::npos);
}

TEST_CASE("MIR alias and effect analysis is independent revision-bound and cacheable") {
  auto lowered = lower_python(
      "base = 40\n"
      "def bump(value):\n"
      "    local = value + 1\n"
      "    return base + local\n"
      "result = bump(1)\n"
      "print(result)\n");
  auto analysis = mpf::detail::analyze_program(lowered.program);
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

TEST_CASE("Analyzer owns complete revision-checked dense semantic side tables") {
  auto lowered = lower_python("values = [[1, 2], [3, 4]]\nprint(values[1][0])\n");
  const auto nodes = lowered.program.node_count;
  const auto hir_seed_before =
      mpf::detail::dump_semantics(mpf::detail::hir::initialize_semantics(lowered.program));
  auto analysis = mpf::detail::analyze_program(lowered.program);
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
  REQUIRE(lowered.program.statements.front().declared_type == mpf::detail::ValueType::unknown);
  REQUIRE(lowered.program.statements.front().shape.empty());
  REQUIRE(mpf::detail::dump_semantics(mpf::detail::hir::initialize_semantics(lowered.program)) ==
          hir_seed_before);
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
  auto unpacked_analysis = mpf::detail::analyze_program(unpacked.program);
  REQUIRE(unpacked_analysis.empty());
  const auto unpack_id = unpacked.program.statements.front().id;
  const auto* unpack_facts = unpacked_analysis.semantics.statement(unpack_id);
  REQUIRE(unpack_facts != nullptr);
  REQUIRE(unpack_facts->target_pattern.valid());
  REQUIRE(unpack_facts->target_pattern.children.size() == 2U);
  REQUIRE(unpacked.program.statements.front().target_pattern.valid());
  REQUIRE(unpacked.program.statements.front().target_pattern.children.front().access_path.empty());
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
  auto analysis = mpf::detail::analyze_program(lowered.program);
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
  auto analysis = mpf::detail::analyze_program(lowered.program);
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
  auto analysis = mpf::detail::analyze_program(lowered.program);
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
  const auto function_scope = analysis.names.owned_scope(function.id);
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
  auto analysis = mpf::detail::analyze_program(lowered.program);
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  const auto alias_effects = mpf::detail::mir::analyze_alias_effects(mir.program);
  REQUIRE(mir.program.functions.size() > 1);
  REQUIRE(mir.program.blocks.size() > 2);
  REQUIRE(mir.program.instructions.size() > 1);
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

TEST_CASE("MIR interns tuple function and reference signatures across call sites") {
  auto python = lower_python(
      "def pair(value):\n"
      "    next_value = value + 1\n"
      "    return value, next_value\n"
      "seed = 20\n"
      "left, right = pair(seed)\n"
      "print(left + right)\n");
  auto python_analysis = mpf::detail::analyze_program(python.program);
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
  auto fortran_analysis = mpf::detail::analyze_program(fortran.program);
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
  REQUIRE(fortran_mir.program.calls.front().argument_storages.front().valid());
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
  invalid_reference_actual.calls.front().argument_storages.front() = {};
  REQUIRE(!mpf::detail::mir::verify(invalid_reference_actual, "bad-reference").empty());

  auto missing_call_edge = fortran_mir.program;
  missing_call_edge.calls.clear();
  REQUIRE(!mpf::detail::mir::verify(missing_call_edge, "missing-call-edge").empty());
}

TEST_CASE("backends create isolated semantic pipelines and strongly typed LIR artifacts") {
  static_assert(
      !std::is_same_v<mpf::detail::javascript::lir::Program, mpf::detail::cpp::lir::Program>);
  static_assert(
      !std::is_same_v<mpf::detail::javascript::lir::Expression, mpf::detail::cpp::lir::Expression>);

  auto lowered = lower_python("print(abs(-2))\n");
  auto analysis = mpf::detail::analyze_program(lowered.program);
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
  REQUIRE(javascript_dump.find("javascript-semantic-lir-v1") != std::string::npos);
  REQUIRE(javascript_dump.find("expr %l") != std::string::npos);
  REQUIRE(cpp_dump.find("cpp-semantic-lir-v1") != std::string::npos);
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

  auto parsed = mpf::detail::parse_with_frontend(frontend, source);
  REQUIRE(parsed.diagnostics.empty());
  REQUIRE(frontend.verify(parsed.ast).empty());
  auto hir = frontend.lower(std::move(parsed.ast));
  REQUIRE(hir.diagnostics.empty());
  auto analysis = mpf::detail::analyze_program(hir.program);
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

TEST_CASE("MIR verifier rejects ownership control-flow and dominance corruption") {
  auto lowered = lower_python(
      "def choose(value):\n"
      "    if value > 0:\n"
      "        return value\n"
      "    return 0\n"
      "print(choose(1))\n");
  auto analysis = mpf::detail::analyze_program(lowered.program);
  REQUIRE(analysis.empty());
  auto lowered_mir = mpf::detail::mir::lower_from_hir(
      std::move(lowered.program), std::move(analysis.semantics), analysis.names);
  REQUIRE(lowered_mir.diagnostics.empty());

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
  auto phi_analysis = mpf::detail::analyze_program(lowered_with_phi.program);
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
