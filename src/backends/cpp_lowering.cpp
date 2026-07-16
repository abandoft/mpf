#include "cpp_lowering.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>

#include "../compiler/function_graph_generic.hpp"
#include "../ir/pass_manager.hpp"
#include "cpp_bindings.hpp"
#include "cpp_lir_planning.hpp"
#include "cpp_lir_representation.hpp"
#include "cpp_renderer.hpp"
#include "target_lir_builder.hpp"
#include "target_lir_dump.hpp"

namespace mpf::detail::cpp {
namespace {

constexpr TargetProfile profile{TargetLanguage::cpp, "C++17", false, true, false};

constexpr LegalizationTable make_legalizations() {
  LegalizationTable result{};
  for (std::size_t index = 1; index < result.size(); ++index) {
    result[index] = LegalizationAction::direct;
  }
  result[static_cast<std::size_t>(mir::Opcode::call)] = LegalizationAction::rewrite;
  result[static_cast<std::size_t>(mir::Opcode::index)] = LegalizationAction::runtime;
  result[static_cast<std::size_t>(mir::Opcode::slice)] = LegalizationAction::runtime;
  result[static_cast<std::size_t>(mir::Opcode::indexed_assignment)] = LegalizationAction::runtime;
  result[static_cast<std::size_t>(mir::Opcode::comparison_chain)] = LegalizationAction::rewrite;
  result[static_cast<std::size_t>(mir::Opcode::conditional)] = LegalizationAction::rewrite;
  return result;
}

constexpr auto legalizations = make_legalizations();

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0008", std::move(message), location});
}

bool array_intrinsic(const IntrinsicId intrinsic) noexcept {
  return intrinsic == IntrinsicId::sum || intrinsic == IntrinsicId::python_length ||
         intrinsic == IntrinsicId::matlab_length || intrinsic == IntrinsicId::element_count ||
         intrinsic == IntrinsicId::reshape;
}

using CallLookup = std::vector<const mir::CallSite*>;

void analyze_expression(const mir::Expression& expression, semantic::Program& result,
                        std::vector<Diagnostic>& diagnostics, const CallLookup& calls) {
  if (!expression.valid()) return;
  if (expression.binding == BindingKind::builtin && expression.intrinsic != IntrinsicId::none) {
    const auto* binding = cpp_code_binding(expression.intrinsic);
    if (binding == nullptr || binding->kind == CodeBindingKind::unavailable) {
      const auto* descriptor = intrinsic_descriptor(expression.intrinsic);
      add_error(diagnostics, expression.location,
                "cpp legalization has no binding for intrinsic '" +
                    std::string(descriptor == nullptr ? "unknown" : descriptor->name) + "'");
    } else if (expression.origin.valid() &&
               static_cast<std::size_t>(expression.origin.value()) < result.bindings.size()) {
      result.bindings[expression.origin.value()] = *binding;
    }
  }
  if (expression.kind == ExpressionKind::index || expression.kind == ExpressionKind::slice) {
    result.runtime.require(lir::RuntimeFeature::arrays);
  }
  if (expression.kind == ExpressionKind::conditional ||
      expression.kind == ExpressionKind::comparison_chain ||
      (expression.kind == ExpressionKind::unary && expression.value == "!") ||
      (expression.kind == ExpressionKind::binary &&
       (expression.value == "&&" || expression.value == "||" ||
        expression.comparison != ComparisonOperator::none))) {
    result.runtime.require(lir::RuntimeFeature::dynamic_values);
  }
  if (expression.kind == ExpressionKind::call && !expression.children.empty()) {
    const auto& callee = expression.children.front();
    if (callee.binding == BindingKind::builtin && array_intrinsic(callee.intrinsic)) {
      result.runtime.require(lir::RuntimeFeature::arrays);
    }
    if (callee.binding == BindingKind::builtin && callee.intrinsic == IntrinsicId::python_float) {
      result.runtime.require(lir::RuntimeFeature::dynamic_values);
    }
    const auto* call = expression.origin.valid() && expression.origin.value() < calls.size()
                           ? calls[expression.origin.value()]
                           : nullptr;
    if (call != nullptr &&
        std::any_of(call->arguments.begin(), call->arguments.end(), [](const auto& argument) {
          return argument_transfer_writes(argument.transfer);
        })) {
      result.runtime.require(lir::RuntimeFeature::reference_arguments);
    }
    if (call != nullptr &&
        std::any_of(call->arguments.begin(), call->arguments.end(), [](const auto& argument) {
          return argument_transfer_forwards_optional(argument.transfer);
        })) {
      result.runtime.require(lir::RuntimeFeature::optional_arguments);
    }
  }
  for (const auto& child : expression.children) {
    analyze_expression(child, result, diagnostics, calls);
  }
}

void analyze_statements(const std::vector<mir::Statement>& statements, semantic::Program& result,
                        std::vector<Diagnostic>& diagnostics, const CallLookup& calls) {
  for (const auto& statement : statements) {
    if ((statement.kind == StatementKind::if_statement ||
         statement.kind == StatementKind::while_loop) &&
        result.source_semantics.truthiness == mpf::detail::semantic::Truthiness::dynamic) {
      result.runtime.require(lir::RuntimeFeature::dynamic_values);
    }
    analyze_expression(statement.expression, result, diagnostics, calls);
    analyze_expression(statement.secondary_expression, result, diagnostics, calls);
    analyze_expression(statement.tertiary_expression, result, diagnostics, calls);
    analyze_expression(statement.target_expression, result, diagnostics, calls);
    for (const auto& expression : statement.parameter_defaults) {
      analyze_expression(expression, result, diagnostics, calls);
    }
    for (const auto& selector : statement.case_selectors) {
      analyze_expression(selector.lower, result, diagnostics, calls);
      analyze_expression(selector.upper, result, diagnostics, calls);
    }
    if (statement.kind == StatementKind::indexed_assignment) {
      result.runtime.require(lir::RuntimeFeature::arrays);
    }
    if (statement.kind == StatementKind::select_case &&
        statement.expression.inferred_type == ValueType::string) {
      result.runtime.require(lir::RuntimeFeature::character_case);
    }
    if (std::any_of(statement.parameter_optional.begin(), statement.parameter_optional.end(),
                    [](const bool optional) { return optional; })) {
      result.runtime.require(lir::RuntimeFeature::optional_arguments);
    }
    analyze_statements(statement.body, result, diagnostics, calls);
    analyze_statements(statement.alternative, result, diagnostics, calls);
  }
}

std::vector<Diagnostic> verify_semantic(const semantic::Program& program) {
  std::vector<Diagnostic> diagnostics;
  if (program.source_language == SourceLanguage::automatic ||
      program.profile.target != TargetLanguage::cpp ||
      !legalization_table_complete(program.legalizations) ||
      program.bindings.size() != program.hir_node_count + 1 ||
      program.function_summary_count == 0 ||
      (program.reads_unknown && !mir::has_effect(program.effects, mir::Effect::read)) ||
      (program.writes_unknown && !mir::has_effect(program.effects, mir::Effect::write))) {
    add_error(diagnostics, {1, 1}, "invalid cpp semantic lowering program");
  }
  return diagnostics;
}

bool contains_slice(const lir::Expression& expression) {
  if (expression.kind == ExpressionKind::slice) return true;
  return std::any_of(expression.children.begin(), expression.children.end(),
                     [](const lir::Expression& child) { return contains_slice(child); });
}

void verify_expression(const lir::Expression& expression, const std::size_t node_count,
                       std::vector<bool>& seen, std::vector<Diagnostic>& diagnostics) {
  if (!expression.valid()) return;
  const auto id = static_cast<std::size_t>(expression.id.value());
  if (!expression.origin.valid() || id == 0 || id > node_count || seen[id]) {
    add_error(diagnostics, expression.location, "cpp LIR expression has invalid identity/origin");
    return;
  }
  seen[id] = true;
  if (expression.binding == BindingKind::builtin && expression.intrinsic != IntrinsicId::none &&
      expression.target_binding.kind == CodeBindingKind::unavailable) {
    add_error(diagnostics, expression.location, "cpp LIR has an unresolved intrinsic binding");
  }
  if (expression.kind == ExpressionKind::call && !expression.argument_transfers.empty() &&
      expression.argument_transfers.size() + 1U != expression.children.size()) {
    add_error(diagnostics, expression.location,
              "cpp LIR call has an invalid argument transfer plan");
  }
  for (std::size_t index = 0; index < expression.argument_transfers.size(); ++index) {
    const auto transfer = expression.argument_transfers[index];
    const auto& argument = expression.children[index + 1U];
    if ((transfer == ArgumentTransfer::omitted) !=
        (argument.kind == ExpressionKind::omitted_argument)) {
      add_error(diagnostics, expression.location,
                "cpp LIR omitted argument disagrees with its transfer plan");
    }
    if (argument_transfer_copies(transfer) && !contains_slice(argument)) {
      add_error(diagnostics, expression.location, "cpp LIR copy transfer has no section actual");
    }
    if (argument_transfer_forwards_optional(transfer) &&
        argument.kind != ExpressionKind::identifier) {
      add_error(diagnostics, expression.location,
                "cpp LIR optional forwarding has no parameter name");
    }
  }
  for (const auto& child : expression.children) {
    verify_expression(child, node_count, seen, diagnostics);
  }
}

void verify_statements(const std::vector<lir::Statement>& statements, const std::size_t node_count,
                       std::vector<bool>& seen, std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    const auto id = static_cast<std::size_t>(statement.id.value());
    if (!statement.origin.valid() || id == 0 || id > node_count || seen[id]) {
      add_error(diagnostics, {statement.line, 1}, "cpp LIR statement has invalid identity/origin");
      continue;
    }
    seen[id] = true;
    verify_expression(statement.expression, node_count, seen, diagnostics);
    verify_expression(statement.secondary_expression, node_count, seen, diagnostics);
    verify_expression(statement.tertiary_expression, node_count, seen, diagnostics);
    verify_expression(statement.target_expression, node_count, seen, diagnostics);
    for (const auto& expression : statement.parameter_defaults) {
      verify_expression(expression, node_count, seen, diagnostics);
    }
    for (const auto& selector : statement.case_selectors) {
      verify_expression(selector.lower, node_count, seen, diagnostics);
      verify_expression(selector.upper, node_count, seen, diagnostics);
    }
    verify_statements(statement.body, node_count, seen, diagnostics);
    verify_statements(statement.alternative, node_count, seen, diagnostics);
  }
}

std::vector<Diagnostic> verify_lir(const lir::SemanticProgram& program) {
  std::vector<Diagnostic> diagnostics;
  if (program.emission.module_top_level == program.emission.entry_function_top_level) {
    add_error(diagnostics, {1, 1}, "cpp LIR must select exactly one top-level storage plan");
  }
  if (!identifier_plan_complete(program.identifiers, collect_identifier_names(program))) {
    add_error(diagnostics, {1, 1}, "cpp LIR identifier allocation plan is incomplete");
  }
  std::vector<bool> seen(program.node_count + 1, false);
  verify_statements(program.statements, program.node_count, seen, diagnostics);
  if (static_cast<std::size_t>(std::count(seen.begin(), seen.end(), true)) != program.node_count) {
    add_error(diagnostics, {1, 1}, "cpp LIR has unreachable node identities");
  }
  verify_lir_resources(program, diagnostics);
  verify_lir_representation(program, diagnostics);
  return diagnostics;
}

std::vector<Diagnostic> verify_lir_stage(const lir::SemanticProgram& program, std::string_view) {
  return verify_lir(program);
}

std::vector<Diagnostic> canonicalize_lir(lir::SemanticProgram& program) {
  std::sort(program.dependencies.begin(), program.dependencies.end());
  program.dependencies.erase(std::unique(program.dependencies.begin(), program.dependencies.end()),
                             program.dependencies.end());
  return {};
}

std::vector<Diagnostic> verify_serialized_lir(const lir::Program& program) {
  std::vector<Diagnostic> diagnostics;
  if (program.chunks.empty() || serialize_chunks(program.chunks).empty()) {
    add_error(diagnostics, {1, 1}, "cpp LIR has no serialized emission chunks");
  }
  if (program.semantic_dump.empty()) {
    add_error(diagnostics, {1, 1}, "cpp LIR has no semantic debug dump");
  }
  if (program.node_count == 0) {
    add_error(diagnostics, {1, 1}, "cpp LIR has no source node inventory");
  }
  if (std::none_of(program.chunks.begin(), program.chunks.end(), [](const auto& chunk) {
        return chunk.source.line != 0 && chunk.origin.valid();
      })) {
    add_error(diagnostics, {1, 1}, "cpp LIR has no source-mapped chunks");
  }
  return diagnostics;
}

}  // namespace

const TargetProfile& target_profile() noexcept {
  return profile;
}

const LegalizationTable& legalization_table() noexcept {
  return legalizations;
}

std::vector<Diagnostic> verify_semantic_lir(const lir::SemanticProgram& program) {
  return verify_lir(program);
}

BackendLoweringResult lower(const mir::Program& program, const mir::AliasEffectTable& alias_effects,
                            const TranspileOptions& options) {
  BackendLoweringResult result;
  if (!mir::alias_effects_current(program, alias_effects)) {
    result.diagnostics = mir::verify_alias_effects(program, alias_effects, "cpp-lowering");
    return result;
  }
  result.diagnostics = validate_legalizations(program, legalizations, "cpp");
  if (!result.diagnostics.empty()) return result;
  semantic::Program semantic_program;
  semantic_program.profile = profile;
  semantic_program.legalizations = legalizations;
  semantic_program.source_semantics = program.semantics;
  semantic_program.source_language = program.source_language;
  semantic_program.hir_node_count = program.hir_node_count;
  semantic_program.function_summary_count = alias_effects.function_count;
  for (std::size_t index = 1; index < alias_effects.functions.size(); ++index) {
    const auto& facts = alias_effects.functions[index];
    semantic_program.effects |= facts.effects;
    semantic_program.reads_unknown = semantic_program.reads_unknown || facts.reads_unknown;
    semantic_program.writes_unknown = semantic_program.writes_unknown || facts.writes_unknown;
  }
  semantic_program.bindings.resize(program.hir_node_count + 1);
  CallLookup calls(program.hir_node_count + 1U);
  for (const auto& call : program.calls) {
    if (call.origin.valid() && call.origin.value() < calls.size())
      calls[call.origin.value()] = &call;
  }
  analyze_statements(program.statements, semantic_program, result.diagnostics, calls);
  auto semantic_diagnostics = verify_semantic(semantic_program);
  result.diagnostics.insert(result.diagnostics.end(),
                            std::make_move_iterator(semantic_diagnostics.begin()),
                            std::make_move_iterator(semantic_diagnostics.end()));
  if (!result.diagnostics.empty()) return result;

  const auto resolve = [&](const HirNodeId origin, const IntrinsicId intrinsic) {
    if (origin.valid() &&
        static_cast<std::size_t>(origin.value()) < semantic_program.bindings.size()) {
      const auto& binding = semantic_program.bindings[origin.value()];
      if (binding.intrinsic == intrinsic) return binding;
    }
    const auto* fallback = cpp_code_binding(intrinsic);
    return fallback == nullptr ? CodeBinding{} : *fallback;
  };
  auto lowered = lower_structured_lir<lir::SemanticProgram, lir::Statement, lir::Expression,
                                      lir::CaseSelector>(program, resolve);
  lowered->runtime = semantic_program.runtime;
  lowered->emission.dynamic_truthiness =
      semantic_program.source_semantics.truthiness == mpf::detail::semantic::Truthiness::dynamic;
  lowered->emission.operand_logical_result = semantic_program.source_semantics.logical_result ==
                                             mpf::detail::semantic::LogicalResult::operand;
  lowered->emission.real_division =
      semantic_program.source_semantics.division == mpf::detail::semantic::Division::real_quotient;
  lowered->emission.resizable_sections = semantic_program.source_semantics.resizable_sections;
  lowered->emission.module_top_level = semantic_program.source_semantics.top_level_storage ==
                                       mpf::detail::semantic::TopLevelStorage::module;
  lowered->emission.entry_function_top_level =
      semantic_program.source_semantics.top_level_storage ==
      mpf::detail::semantic::TopLevelStorage::entry_function;
  lowered->identifiers =
      allocate_identifiers(TargetLanguage::cpp, collect_identifier_names(*lowered));
  lowered->dependencies = semantic_program.dependencies;
  lowered->function_graph =
      build_function_dependency_graph_generic<lir::Expression, lir::Statement>(lowered->statements);
  plan_lir_resources(*lowered, options);
  plan_lir_representation(*lowered);
  PassManager<lir::SemanticProgram> passes(&verify_lir_stage);
  passes.add({"cpp-lir-canonicalization", &canonicalize_lir, true});
  auto lir_diagnostics = passes.run(*lowered);
  result.diagnostics.insert(result.diagnostics.end(),
                            std::make_move_iterator(lir_diagnostics.begin()),
                            std::make_move_iterator(lir_diagnostics.end()));
  if (result.diagnostics.empty()) {
    auto artifact = std::make_unique<lir::Program>();
    artifact->dependency_names = lowered->dependencies;
    artifact->node_count = lowered->node_count;
    artifact->revision = lowered->revision;
    artifact->semantic_dump = lir::dump(*lowered);
    artifact->chunks = materialize_chunks(render_cpp(*lowered));
    auto artifact_diagnostics = verify_serialized_lir(*artifact);
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(artifact_diagnostics.begin()),
                              std::make_move_iterator(artifact_diagnostics.end()));
    if (result.diagnostics.empty()) result.artifact = std::move(artifact);
  }
  return result;
}

std::string lir::dump(const SemanticProgram& program) {
  std::ostringstream output;
  dump_target_lir_body(output, program, "cpp");
  output << "function-abis\n";
  const auto dump_scope = [&](const std::string_view label, const ScopePlan& scope) {
    output << label << '\n';
    for (const auto& declaration : scope.declarations) {
      output << "  declaration " << std::quoted(declaration.name) << " type-kind "
             << static_cast<int>(declaration.type_kind) << " type "
             << std::quoted(declaration.concrete_type) << " probe %l"
             << declaration.type_probe.value() << " tuple ";
      if (declaration.tuple_index == dynamic_extent)
        output << "none";
      else
        output << declaration.tuple_index;
      output << " shape [";
      for (std::size_t index = 0; index < declaration.fixed_shape.size(); ++index) {
        if (index != 0) output << ',';
        output << declaration.fixed_shape[index];
      }
      output << "] nested-types [";
      for (std::size_t index = 0; index < declaration.fixed_nested_types.size(); ++index) {
        if (index != 0) output << ',';
        output << std::quoted(declaration.fixed_nested_types[index]);
      }
      output << "]\n";
    }
  };
  const auto dump_abis = [&](const auto& self, const std::vector<Statement>& statements) -> void {
    for (const auto& statement : statements) {
      if (statement.function_abi.valid) {
        output << "  %l" << statement.id.value()
               << " recursive=" << statement.function_abi.recursive
               << " forward=" << statement.function_abi.forward_declarable << " return "
               << std::quoted(statement.function_abi.return_type) << " parameters [";
        for (std::size_t index = 0; index < statement.function_abi.parameters.size(); ++index) {
          if (index != 0) output << ',';
          const auto& parameter = statement.function_abi.parameters[index];
          output << static_cast<int>(parameter.passing) << ':'
                 << std::quoted(parameter.concrete_type) << ':'
                 << std::quoted(parameter.template_parameter);
        }
        output << "]\n";
        dump_scope("  function-scope %l" + std::to_string(statement.id.value()),
                   statement.function_scope);
      }
      self(self, statement.body);
      self(self, statement.alternative);
    }
  };
  dump_abis(dump_abis, program.statements);
  dump_scope("program-scope", program.program_scope);
  output << "translation-unit banner=" << program.translation_unit.emit_banner
         << " runtime-namespace " << std::quoted(program.translation_unit.runtime_namespace)
         << " generated-namespace " << std::quoted(program.translation_unit.generated_namespace)
         << " headers [";
  for (std::size_t index = 0; index < program.translation_unit.standard_headers.size(); ++index) {
    if (index != 0) output << ',';
    output << std::quoted(program.translation_unit.standard_headers[index]);
  }
  output << "] runtime [";
  for (std::size_t index = 0; index < program.translation_unit.runtime_fragments.size(); ++index) {
    if (index != 0) output << ',';
    output << static_cast<int>(program.translation_unit.runtime_fragments[index]);
  }
  const auto dump_order = [&](const std::string_view label, const std::vector<std::size_t>& order) {
    output << "] " << label << " [";
    for (std::size_t index = 0; index < order.size(); ++index) {
      if (index != 0) output << ',';
      output << order[index];
    }
  };
  dump_order("forward", program.translation_unit.forward_declarations);
  dump_order("definitions", program.translation_unit.definitions);
  dump_order("entry", program.translation_unit.entry_statements);
  output << "] module-scope=" << program.translation_unit.emit_module_scope
         << " entry-scope=" << program.translation_unit.entry_owns_program_scope
         << " emit-entry=" << program.translation_unit.emit_entry_function
         << " emit-main=" << program.translation_unit.emit_main << '\n';
  output << "emission dynamic-truthiness=" << program.emission.dynamic_truthiness
         << " operand-logical-result=" << program.emission.operand_logical_result
         << " real-division=" << program.emission.real_division
         << " resizable-sections=" << program.emission.resizable_sections
         << " module-top-level=" << program.emission.module_top_level
         << " entry-function-top-level=" << program.emission.entry_function_top_level << '\n';
  output << "function-order";
  for (const auto index : program.function_graph.definition_order) output << ' ' << index;
  output << '\n';
  return output.str();
}

std::string lir::dump(const Program& program) {
  return program.semantic_dump;
}

std::vector<Diagnostic> verify_artifact(const BackendArtifact& artifact) {
  if (artifact.target() != TargetLanguage::cpp) {
    return {{DiagnosticSeverity::error,
             "MPF0008",
             "cpp backend received an artifact for another target",
             {1, 1}}};
  }
  return verify_serialized_lir(static_cast<const lir::Program&>(artifact));
}

}  // namespace mpf::detail::cpp
