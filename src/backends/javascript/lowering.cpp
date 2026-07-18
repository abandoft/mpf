#include "lowering.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <utility>

#include "backends/common/lir_builder.hpp"
#include "backends/common/lir_dump.hpp"
#include "bindings.hpp"
#include "ir/pass_manager.hpp"
#include "lir_planning.hpp"
#include "lir_representation.hpp"
#include "renderer.hpp"

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

constexpr bool scalar_division_operation(const BinaryOperator operation) noexcept {
  return operation == BinaryOperator::divide || operation == BinaryOperator::floor_divide ||
         operation == BinaryOperator::left_divide ||
         operation == BinaryOperator::elementwise_divide ||
         operation == BinaryOperator::elementwise_left_divide;
}

bool has_array_operand(const mir::Program& program, const mir::Expression& expression,
                       const mir::ExpressionAttributes& attributes) noexcept {
  if (attributes.broadcast.valid ||
      mir::value_type(program, expression.type_id) == ValueType::list) {
    return true;
  }
  return std::any_of(
      expression.children.begin(), expression.children.end(), [&](const MirExpressionId child_id) {
        const auto* child = mir::expression(program, child_id);
        return child != nullptr && mir::value_type(program, child->type_id) == ValueType::list;
      });
}

bool array_intrinsic(const IntrinsicId intrinsic) noexcept {
  return intrinsic == IntrinsicId::sum || intrinsic == IntrinsicId::logical_all ||
         intrinsic == IntrinsicId::logical_any || intrinsic == IntrinsicId::python_length ||
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
  if (program.source_language == SourceLanguage::matlab &&
      expression.kind == ExpressionKind::list && expression.children.empty()) {
    result.runtime.require(lir::RuntimeFeature::arrays);
  }
  if (expression.kind == ExpressionKind::unary &&
      (attributes.unary_operation == UnaryOperator::transpose ||
       attributes.unary_operation == UnaryOperator::conjugate_transpose)) {
    result.runtime.require(lir::RuntimeFeature::arrays);
  }
  if (program.source_language == SourceLanguage::matlab &&
      expression.kind == ExpressionKind::binary &&
      attributes.array_operation == mpf::detail::semantic::ArrayOperation::matlab) {
    result.runtime.require(lir::RuntimeFeature::arrays);
  }
  if (expression.kind == ExpressionKind::binary &&
      scalar_division_operation(attributes.operation) &&
      !has_array_operand(program, expression, attributes) &&
      result.source_semantics.division_by_zero ==
          mpf::detail::semantic::DivisionByZero::exception) {
    result.runtime.require(lir::RuntimeFeature::scalar_division);
  }
  if (program.source_language == SourceLanguage::matlab &&
      attributes.logical_evaluation != mpf::detail::semantic::LogicalEvaluation::none) {
    result.runtime.require(lir::RuntimeFeature::arrays);
  }
  const auto dynamic_truthiness =
      result.source_semantics.truthiness == mpf::detail::semantic::Truthiness::dynamic;
  const auto operand_logical =
      result.source_semantics.logical_result == mpf::detail::semantic::LogicalResult::operand;
  const auto structural_comparison =
      result.source_semantics.equality == mpf::detail::semantic::Equality::structural;
  const auto comparison_runtime = [&](const ComparisonOperator operation) {
    return structural_comparison || comparison_is_identity(operation) ||
           comparison_is_membership(operation);
  };
  const auto chain_runtime =
      expression.kind == ExpressionKind::comparison_chain &&
      (structural_comparison || std::any_of(attributes.comparisons.begin(),
                                            attributes.comparisons.end(), comparison_runtime));
  if ((expression.kind == ExpressionKind::conditional && dynamic_truthiness) || chain_runtime ||
      expression.kind == ExpressionKind::tuple ||
      (expression.kind == ExpressionKind::unary && attributes.spelling == "!" &&
       dynamic_truthiness) ||
      (expression.kind == ExpressionKind::binary &&
       (((attributes.operation == BinaryOperator::logical_and ||
          attributes.operation == BinaryOperator::logical_or) &&
         operand_logical) ||
        (attributes.comparison != ComparisonOperator::none &&
         comparison_runtime(attributes.comparison))))) {
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
    if ((statement.kind == StatementKind::if_statement ||
         statement.kind == StatementKind::while_loop) &&
        result.source_semantics.truthiness ==
            mpf::detail::semantic::Truthiness::matlab_all_nonzero) {
      result.runtime.require(lir::RuntimeFeature::arrays);
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
    if (program.source_language == SourceLanguage::fortran &&
        statement.kind == StatementKind::select_case && selector != nullptr &&
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
      !mpf::detail::semantic::valid_division_contract(program.source_semantics.division,
                                                      program.source_semantics.division_by_zero) ||
      (program.source_language != SourceLanguage::automatic &&
       !mpf::detail::semantic::source_division_contract_matches(program.source_language,
                                                                program.source_semantics)) ||
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
  if (expression.kind == ExpressionKind::binary) {
    const bool has_comparison = expression.comparison != ComparisonOperator::none;
    const bool has_operation = expression.operation != BinaryOperator::none;
    if (has_comparison == has_operation || (has_comparison && !expression.value.empty()) ||
        (has_operation && expression.value.empty())) {
      add_error(diagnostics, expression.location,
                "JavaScript LIR binary operator identity is missing or ambiguous");
    }
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
  if (!mpf::detail::semantic::valid_division_contract(program.emission.division,
                                                      program.emission.division_by_zero)) {
    add_error(diagnostics, {1, 1}, "JavaScript LIR division policy is internally inconsistent");
  }
  if (program.source_language != SourceLanguage::automatic &&
      program.emission.padded_character_selection !=
          (program.source_language == SourceLanguage::fortran)) {
    add_error(diagnostics, {1, 1},
              "JavaScript LIR character-selection policy disagrees with the source semantics");
  }
  if (program.source_language != SourceLanguage::automatic &&
      program.emission.matlab_truthiness != (program.source_language == SourceLanguage::matlab)) {
    add_error(diagnostics, {1, 1},
              "JavaScript LIR Matlab truthiness policy disagrees with the source language");
  }
  if (program.source_language != SourceLanguage::automatic &&
      !mpf::detail::semantic::source_division_contract_matches(
          program.source_language, program.emission.division, program.emission.division_by_zero)) {
    add_error(diagnostics, {1, 1},
              "JavaScript LIR division policy disagrees with the source language");
  }
  if (program.identifiers.target != TargetLanguage::javascript ||
      !identifier_plan_complete(program.identifiers, collect_identifier_inventory(program))) {
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
  lowered->emission.matlab_truthiness = semantic_program.source_semantics.truthiness ==
                                        mpf::detail::semantic::Truthiness::matlab_all_nonzero;
  lowered->emission.operand_logical_result = semantic_program.source_semantics.logical_result ==
                                             mpf::detail::semantic::LogicalResult::operand;
  lowered->emission.division = semantic_program.source_semantics.division;
  lowered->emission.division_by_zero = semantic_program.source_semantics.division_by_zero;
  lowered->emission.explicit_exports_only = semantic_program.source_semantics.export_policy ==
                                            mpf::detail::semantic::ExportPolicy::explicit_only;
  lowered->emission.lexical_block_scopes = semantic_program.source_semantics.scope_model ==
                                           mpf::detail::semantic::ScopeModel::lexical_blocks;
  lowered->emission.structural_equality =
      semantic_program.source_semantics.equality == mpf::detail::semantic::Equality::structural;
  lowered->emission.resizable_sections = semantic_program.source_semantics.resizable_sections;
  lowered->emission.emit_parameter_defaults =
      semantic_program.source_semantics.emit_parameter_defaults;
  lowered->emission.padded_character_selection =
      semantic_program.source_language == SourceLanguage::fortran;
  lowered->emission.module = options.module_kind == ModuleKind::esm
                                 ? lir::EmissionPlan::ModuleFormat::esm
                                 : lir::EmissionPlan::ModuleFormat::strict_script;
  lowered->identifiers =
      allocate_identifiers(TargetLanguage::javascript, collect_identifier_inventory(*lowered));
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
  const auto dump_scope = [&](const ScopePlan& scope) {
    output << '[';
    for (std::size_t index = 0; index < scope.declarations.size(); ++index) {
      if (index != 0) output << ',';
      output << "@s" << scope.declarations[index].symbol.value() << ':'
             << std::quoted(scope.declarations[index].name);
    }
    output << ']';
  };
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
        output << "] scope ";
        dump_scope(statement.function_scope);
        output << "\n";
      }
      if (statement.statement_scope.valid || statement.body_scope.valid ||
          statement.alternative_scope.valid) {
        output << "  lexical %l" << statement.id.value() << " statement=";
        dump_scope(statement.statement_scope);
        output << " body=";
        dump_scope(statement.body_scope);
        output << " alternative=";
        dump_scope(statement.alternative_scope);
        output << '\n';
      }
      self(self, statement.body);
      self(self, statement.alternative);
    }
  };
  dump_abis(dump_abis, program.statements);
  output << "program-scope ";
  dump_scope(program.program_scope);
  output << "\n";
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
         << " matlab-truthiness=" << program.emission.matlab_truthiness
         << " operand-logical-result=" << program.emission.operand_logical_result
         << " division=" << static_cast<int>(program.emission.division)
         << " division-by-zero=" << static_cast<int>(program.emission.division_by_zero)
         << " explicit-exports-only=" << program.emission.explicit_exports_only
         << " lexical-block-scopes=" << program.emission.lexical_block_scopes
         << " structural-equality=" << program.emission.structural_equality
         << " resizable-sections=" << program.emission.resizable_sections
         << " parameter-defaults=" << program.emission.emit_parameter_defaults
         << " padded-character-selection=" << program.emission.padded_character_selection
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
