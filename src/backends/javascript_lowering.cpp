#include "javascript_lowering.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <utility>

#include "../ir/pass_manager.hpp"
#include "javascript_bindings.hpp"
#include "javascript_lir_planning.hpp"
#include "javascript_lir_representation.hpp"
#include "javascript_renderer.hpp"
#include "target_lir_builder.hpp"
#include "target_lir_dump.hpp"

namespace mpf::detail::javascript {
namespace {

constexpr TargetProfile profile{TargetLanguage::javascript, "ECMAScript", true, true, true};

constexpr LegalizationTable make_legalizations() {
  LegalizationTable result{};
  for (std::size_t index = 1; index < result.size(); ++index) {
    result[index] = LegalizationAction::direct;
  }
  result[static_cast<std::size_t>(mir::Opcode::call)] = LegalizationAction::rewrite;
  result[static_cast<std::size_t>(mir::Opcode::index)] = LegalizationAction::runtime;
  result[static_cast<std::size_t>(mir::Opcode::slice)] = LegalizationAction::runtime;
  result[static_cast<std::size_t>(mir::Opcode::store_indexed)] = LegalizationAction::runtime;
  result[static_cast<std::size_t>(mir::Opcode::copy)] = LegalizationAction::rewrite;
  result[static_cast<std::size_t>(mir::Opcode::writeback)] = LegalizationAction::rewrite;
  result[static_cast<std::size_t>(mir::Opcode::comparison_chain)] = LegalizationAction::rewrite;
  result[static_cast<std::size_t>(mir::Opcode::conditional)] = LegalizationAction::rewrite;
  return result;
}

constexpr auto legalizations = make_legalizations();

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0007", std::move(message), location});
}

bool array_intrinsic(const IntrinsicId intrinsic) noexcept {
  return intrinsic == IntrinsicId::sum || intrinsic == IntrinsicId::python_length ||
         intrinsic == IntrinsicId::matlab_length || intrinsic == IntrinsicId::element_count ||
         intrinsic == IntrinsicId::reshape;
}

using CallLookup = std::vector<const mir::CallSite*>;

void analyze_expression(const mir::Program& program, const MirExpressionId expression_id,
                        semantic::Program& result, std::vector<Diagnostic>& diagnostics,
                        const CallLookup& calls) {
  const auto* expression_node = mir::expression(program, expression_id);
  const auto* expression_attributes = mir::attributes(program, expression_id);
  if (expression_node == nullptr || expression_attributes == nullptr) return;
  const auto& expression = *expression_node;
  const auto& attributes = *expression_attributes;
  if (attributes.binding == BindingKind::builtin && attributes.intrinsic != IntrinsicId::none) {
    const auto* binding = javascript_code_binding(attributes.intrinsic);
    if (binding == nullptr || binding->kind == CodeBindingKind::unavailable) {
      const auto* descriptor = intrinsic_descriptor(attributes.intrinsic);
      add_error(diagnostics, expression.location,
                "JavaScript legalization has no binding for intrinsic '" +
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
      expression.kind == ExpressionKind::tuple ||
      (expression.kind == ExpressionKind::unary && attributes.spelling == "!") ||
      (expression.kind == ExpressionKind::binary &&
       (attributes.spelling == "&&" || attributes.spelling == "||" ||
        attributes.comparison != ComparisonOperator::none))) {
    result.runtime.require(lir::RuntimeFeature::dynamic_values);
  }
  if (expression.kind == ExpressionKind::call && !expression.children.empty()) {
    const auto* callee = mir::expression(program, expression.children.front());
    const auto* callee_attributes = mir::attributes(program, expression.children.front());
    if (callee != nullptr && callee_attributes != nullptr &&
        callee_attributes->binding == BindingKind::builtin &&
        array_intrinsic(callee_attributes->intrinsic)) {
      result.runtime.require(lir::RuntimeFeature::arrays);
    }
    if (callee != nullptr && callee_attributes != nullptr &&
        callee_attributes->binding == BindingKind::builtin &&
        callee_attributes->intrinsic == IntrinsicId::python_float) {
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
  }
  for (const auto child : expression.children) {
    analyze_expression(program, child, result, diagnostics, calls);
  }
}

void analyze_statements(const mir::Program& program, const std::vector<MirStatementId>& statements,
                        semantic::Program& result, std::vector<Diagnostic>& diagnostics,
                        const CallLookup& calls) {
  for (const auto statement_id : statements) {
    const auto* statement_node = mir::statement(program, statement_id);
    if (statement_node == nullptr) continue;
    const auto& statement = *statement_node;
    if ((statement.kind == StatementKind::if_statement ||
         statement.kind == StatementKind::while_loop) &&
        result.source_semantics.truthiness == mpf::detail::semantic::Truthiness::dynamic) {
      result.runtime.require(lir::RuntimeFeature::dynamic_values);
    }
    analyze_expression(program, statement.expression, result, diagnostics, calls);
    analyze_expression(program, statement.secondary_expression, result, diagnostics, calls);
    analyze_expression(program, statement.tertiary_expression, result, diagnostics, calls);
    analyze_expression(program, statement.target_expression, result, diagnostics, calls);
    for (const auto expression : statement.parameter_defaults) {
      analyze_expression(program, expression, result, diagnostics, calls);
    }
    for (const auto& selector : statement.case_selectors) {
      analyze_expression(program, selector.lower, result, diagnostics, calls);
      analyze_expression(program, selector.upper, result, diagnostics, calls);
    }
    if (statement.kind == StatementKind::indexed_assignment) {
      result.runtime.require(lir::RuntimeFeature::arrays);
    }
    const auto* selector = mir::expression(program, statement.expression);
    if (statement.kind == StatementKind::select_case && selector != nullptr &&
        mir::value_type(program, selector->type_id) == ValueType::string) {
      result.runtime.require(lir::RuntimeFeature::character_case);
    }
    analyze_statements(program, statement.body, result, diagnostics, calls);
    analyze_statements(program, statement.alternative, result, diagnostics, calls);
  }
}

std::vector<Diagnostic> verify_semantic(const semantic::Program& program) {
  std::vector<Diagnostic> diagnostics;
  if (program.source_language == SourceLanguage::automatic ||
      program.profile.target != TargetLanguage::javascript ||
      !legalization_table_complete(program.legalizations) ||
      program.bindings.size() != program.hir_node_count + 1 ||
      program.function_summary_count == 0 ||
      (program.reads_unknown && !mir::has_effect(program.effects, mir::Effect::read)) ||
      (program.writes_unknown && !mir::has_effect(program.effects, mir::Effect::write))) {
    add_error(diagnostics, {1, 1}, "invalid JavaScript semantic lowering program");
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
    add_error(diagnostics, expression.location,
              "JavaScript LIR expression has invalid identity or origin");
    return;
  }
  seen[id] = true;
  if (expression.binding == BindingKind::builtin && expression.intrinsic != IntrinsicId::none &&
      expression.target_binding.kind == CodeBindingKind::unavailable) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR contains an unresolved intrinsic binding");
  }
  if (expression.kind == ExpressionKind::call && !expression.argument_transfers.empty() &&
      expression.argument_transfers.size() + 1U != expression.children.size()) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR call has an invalid argument transfer plan");
  }
  for (std::size_t index = 0; index < expression.argument_transfers.size(); ++index) {
    const auto transfer = expression.argument_transfers[index];
    const auto& argument = expression.children[index + 1U];
    if ((transfer == ArgumentTransfer::omitted) !=
        (argument.kind == ExpressionKind::omitted_argument)) {
      add_error(diagnostics, expression.location,
                "JavaScript LIR omitted argument disagrees with its transfer plan");
    }
    if (argument_transfer_copies(transfer) && !contains_slice(argument)) {
      add_error(diagnostics, expression.location,
                "JavaScript LIR copy transfer has no section actual");
    }
    if (argument_transfer_forwards_optional(transfer) &&
        argument.kind != ExpressionKind::identifier) {
      add_error(diagnostics, expression.location,
                "JavaScript LIR optional forwarding has no parameter name");
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
      add_error(diagnostics, {statement.line, 1},
                "JavaScript LIR statement has invalid identity or origin");
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
  if (!identifier_plan_complete(program.identifiers, collect_identifier_names(program))) {
    add_error(diagnostics, {1, 1}, "JavaScript LIR identifier allocation plan is incomplete");
  }
  std::vector<bool> seen(program.node_count + 1, false);
  verify_statements(program.statements, program.node_count, seen, diagnostics);
  if (static_cast<std::size_t>(std::count(seen.begin(), seen.end(), true)) != program.node_count) {
    add_error(diagnostics, {1, 1}, "JavaScript LIR has unreachable node identities");
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
    add_error(diagnostics, {1, 1}, "JavaScript LIR has no serialized emission chunks");
  }
  if (program.semantic_dump.empty()) {
    add_error(diagnostics, {1, 1}, "JavaScript LIR has no semantic debug dump");
  }
  if (program.node_count == 0) {
    add_error(diagnostics, {1, 1}, "JavaScript LIR has no source node inventory");
  }
  if (std::none_of(program.chunks.begin(), program.chunks.end(), [](const auto& chunk) {
        return chunk.source.line != 0 && chunk.origin.valid();
      })) {
    add_error(diagnostics, {1, 1}, "JavaScript LIR has no source-mapped chunks");
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
    result.diagnostics = mir::verify_alias_effects(program, alias_effects, "javascript-lowering");
    return result;
  }
  result.diagnostics = validate_legalizations(program, legalizations, "javascript");
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
  analyze_statements(program, program.roots, semantic_program, result.diagnostics, calls);
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
    const auto* fallback = javascript_code_binding(intrinsic);
    return fallback == nullptr ? CodeBinding{} : *fallback;
  };
  auto lowered = lower_structured_lir<lir::SemanticProgram, lir::Statement, lir::Expression,
                                      lir::CaseSelector>(program, resolve);
  lowered->runtime = semantic_program.runtime;
  lowered->emission.dynamic_truthiness =
      semantic_program.source_semantics.truthiness == mpf::detail::semantic::Truthiness::dynamic;
  lowered->emission.operand_logical_result = semantic_program.source_semantics.logical_result ==
                                             mpf::detail::semantic::LogicalResult::operand;
  lowered->emission.explicit_exports_only = semantic_program.source_semantics.export_policy ==
                                            mpf::detail::semantic::ExportPolicy::explicit_only;
  lowered->emission.structural_equality =
      semantic_program.source_semantics.equality == mpf::detail::semantic::Equality::structural;
  lowered->emission.resizable_sections = semantic_program.source_semantics.resizable_sections;
  lowered->emission.emit_parameter_defaults =
      semantic_program.source_semantics.emit_parameter_defaults;
  lowered->emission.module = options.module_kind == ModuleKind::esm
                                 ? lir::EmissionPlan::ModuleFormat::esm
                                 : lir::EmissionPlan::ModuleFormat::strict_script;
  lowered->identifiers =
      allocate_identifiers(TargetLanguage::javascript, collect_identifier_names(*lowered));
  lowered->dependencies = semantic_program.dependencies;
  plan_lir_resources(*lowered, options);
  plan_lir_representation(*lowered);
  PassManager<lir::SemanticProgram> passes(&verify_lir_stage);
  passes.add({"javascript-lir-canonicalization", &canonicalize_lir, true});
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
    artifact->chunks = materialize_chunks(render_javascript(*lowered));
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
  dump_target_lir_body(output, program, "javascript");
  output << "function-abis\n";
  const auto dump_abis = [&](const auto& self, const std::vector<Statement>& statements) -> void {
    for (const auto& statement : statements) {
      if (statement.function_abi.valid) {
        output << "  %l" << statement.id.value() << " exported=" << statement.function_abi.exported
               << " parameters [";
        for (std::size_t index = 0; index < statement.function_abi.parameters.size(); ++index) {
          if (index != 0) output << ',';
          output << static_cast<int>(statement.function_abi.parameters[index]);
        }
        output << "] scope [";
        for (std::size_t index = 0; index < statement.function_scope.declarations.size(); ++index) {
          if (index != 0) output << ',';
          output << std::quoted(statement.function_scope.declarations[index]);
        }
        output << "]\n";
      }
      self(self, statement.body);
      self(self, statement.alternative);
    }
  };
  dump_abis(dump_abis, program.statements);
  output << "program-scope [";
  for (std::size_t index = 0; index < program.program_scope.declarations.size(); ++index) {
    if (index != 0) output << ',';
    output << std::quoted(program.program_scope.declarations[index]);
  }
  output << "]\n";
  output << "module banner=" << program.module.emit_banner << " directives [";
  for (std::size_t index = 0; index < program.module.directives.size(); ++index) {
    if (index != 0) output << ',';
    output << std::quoted(program.module.directives[index]);
  }
  output << "] runtime [";
  for (std::size_t index = 0; index < program.module.runtime_fragments.size(); ++index) {
    if (index != 0) output << ',';
    output << static_cast<int>(program.module.runtime_fragments[index]);
  }
  output << "] body [";
  for (std::size_t index = 0; index < program.module.body_order.size(); ++index) {
    if (index != 0) output << ',';
    output << program.module.body_order[index];
  }
  output << "]\n";
  output << "emission dynamic-truthiness=" << program.emission.dynamic_truthiness
         << " operand-logical-result=" << program.emission.operand_logical_result
         << " explicit-exports-only=" << program.emission.explicit_exports_only
         << " structural-equality=" << program.emission.structural_equality
         << " resizable-sections=" << program.emission.resizable_sections
         << " parameter-defaults=" << program.emission.emit_parameter_defaults
         << " module=" << static_cast<int>(program.emission.module) << '\n';
  return output.str();
}

std::string lir::dump(const Program& program) {
  return program.semantic_dump;
}

std::vector<Diagnostic> verify_artifact(const BackendArtifact& artifact) {
  if (artifact.target() != TargetLanguage::javascript) {
    return {{DiagnosticSeverity::error,
             "MPF0007",
             "JavaScript backend received an artifact for another target",
             {1, 1}}};
  }
  return verify_serialized_lir(static_cast<const lir::Program&>(artifact));
}

}  // namespace mpf::detail::javascript
