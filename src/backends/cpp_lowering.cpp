#include "cpp_lowering.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "../compiler/function_graph_generic.hpp"
#include "../ir/pass_manager.hpp"
#include "cpp_bindings.hpp"
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
       (expression.value == "&&" || expression.value == "||" || expression.value == "===" ||
        expression.value == "!=="))) {
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

const char* cpp_type(const ValueType type) noexcept {
  switch (type) {
    case ValueType::integer: return "std::int64_t";
    case ValueType::real: return "double";
    case ValueType::boolean: return "bool";
    case ValueType::string: return "std::string";
    case ValueType::null_value: return "std::nullptr_t";
    case ValueType::unknown:
    case ValueType::list:
    case ValueType::tuple:
    case ValueType::function: return "double";
  }
  return "double";
}

std::string cpp_container_type(const ValueType element_type, const std::size_t dimensions) {
  std::string result;
  for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
    result += "std::vector<";
  }
  result += cpp_type(element_type);
  result.append(dimensions, '>');
  return result;
}

std::string cpp_parameter_type(const lir::Statement& statement, const std::size_t index) {
  const auto type = index < statement.parameter_types.size() ? statement.parameter_types[index]
                                                             : ValueType::unknown;
  if (type != ValueType::list) return cpp_type(type);
  const auto element = index < statement.parameter_element_types.size()
                           ? statement.parameter_element_types[index]
                           : ValueType::unknown;
  const auto rank =
      index < statement.parameter_shapes.size() ? statement.parameter_shapes[index].size() : 0U;
  return cpp_container_type(element, rank);
}

bool primitive_type(const ValueType type) noexcept {
  return type == ValueType::integer || type == ValueType::real || type == ValueType::boolean ||
         type == ValueType::string || type == ValueType::null_value;
}

bool explicit_return_type(const lir::Statement& statement, std::string& type) {
  if (statement.return_names.empty() && !statement.has_value_return) {
    type = "void";
    return true;
  }
  const bool tuple_return =
      statement.return_names.size() > 1 ||
      (statement.declared_type == ValueType::tuple && !statement.return_types.empty());
  if (tuple_return) {
    if ((!statement.return_names.empty() &&
         statement.return_types.size() != statement.return_names.size()) ||
        !std::all_of(statement.return_types.begin(), statement.return_types.end(),
                     [](const ValueType value) { return primitive_type(value); })) {
      return false;
    }
    type = "std::tuple<";
    for (std::size_t index = 0; index < statement.return_types.size(); ++index) {
      if (index != 0) type += ", ";
      type += cpp_type(statement.return_types[index]);
    }
    type += '>';
    return true;
  }
  if (!primitive_type(statement.declared_type)) return false;
  type = cpp_type(statement.declared_type);
  return true;
}

const char* temporary_stem(const lir::TemporaryRole role) noexcept {
  switch (role) {
    case lir::TemporaryRole::comparison_operand: return "comparison";
    case lir::TemporaryRole::section_argument: return "section_reference";
    case lir::TemporaryRole::call_result: return "call_result";
    case lir::TemporaryRole::select_value: return "select";
    case lir::TemporaryRole::assignment_value: return "outputs";
    case lir::TemporaryRole::loop_completed: return "loop_completed";
    case lir::TemporaryRole::range_start: return "start";
    case lir::TemporaryRole::range_stop: return "stop";
    case lir::TemporaryRole::range_step: return "step";
    case lir::TemporaryRole::range_first: return "range_first";
    case lir::TemporaryRole::range_cursor: return "cursor";
  }
  return "temporary";
}

void add_temporary(lir::SemanticProgram& program, std::set<std::string>& used, const LirNodeId node,
                   const lir::TemporaryRole role, const std::size_t ordinal = 0) {
  while (program.temporaries.offsets.size() <= node.value()) {
    program.temporaries.offsets.push_back(
        static_cast<std::uint32_t>(program.temporaries.slots.size()));
  }
  program.temporaries.slots.push_back(
      {role, static_cast<std::uint32_t>(ordinal),
       reserve_internal_identifier(used, temporary_stem(role), node.value(), ordinal)});
}

void plan_expression_temporaries(lir::SemanticProgram& program, const lir::Expression& expression,
                                 std::set<std::string>& used) {
  if (!expression.valid()) return;
  if (expression.kind == ExpressionKind::comparison_chain) {
    for (std::size_t index = 0; index < expression.children.size(); ++index) {
      add_temporary(program, used, expression.id, lir::TemporaryRole::comparison_operand, index);
    }
  }
  if (expression.kind == ExpressionKind::call) {
    bool copied = false;
    for (std::size_t index = 0; index < expression.argument_transfers.size(); ++index) {
      if (!argument_transfer_copies(expression.argument_transfers[index])) continue;
      copied = true;
      add_temporary(program, used, expression.id, lir::TemporaryRole::section_argument, index);
    }
    if (copied && expression.procedure_has_result) {
      add_temporary(program, used, expression.id, lir::TemporaryRole::call_result);
    }
  }
  for (const auto& child : expression.children) {
    plan_expression_temporaries(program, child, used);
  }
}

void plan_statement_temporaries(lir::SemanticProgram& program,
                                const std::vector<lir::Statement>& statements,
                                std::set<std::string>& used) {
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::select_case) {
      add_temporary(program, used, statement.id, lir::TemporaryRole::select_value);
    } else if (statement.kind == StatementKind::multi_assignment) {
      add_temporary(program, used, statement.id, lir::TemporaryRole::assignment_value);
    } else if (statement.kind == StatementKind::while_loop && !statement.alternative.empty()) {
      add_temporary(program, used, statement.id, lir::TemporaryRole::loop_completed);
    } else if (statement.kind == StatementKind::range_loop) {
      add_temporary(program, used, statement.id, lir::TemporaryRole::range_start);
      add_temporary(program, used, statement.id, lir::TemporaryRole::range_stop);
      add_temporary(program, used, statement.id, lir::TemporaryRole::range_step);
      add_temporary(program, used, statement.id, lir::TemporaryRole::range_first);
      if (statement.retain_last_loop_value) {
        add_temporary(program, used, statement.id, lir::TemporaryRole::range_cursor);
      }
      if (!statement.alternative.empty()) {
        add_temporary(program, used, statement.id, lir::TemporaryRole::loop_completed);
      }
    }
    plan_expression_temporaries(program, statement.expression, used);
    plan_expression_temporaries(program, statement.secondary_expression, used);
    plan_expression_temporaries(program, statement.tertiary_expression, used);
    plan_expression_temporaries(program, statement.target_expression, used);
    for (const auto& expression : statement.parameter_defaults) {
      plan_expression_temporaries(program, expression, used);
    }
    for (const auto& selector : statement.case_selectors) {
      plan_expression_temporaries(program, selector.lower, used);
      plan_expression_temporaries(program, selector.upper, used);
    }
    plan_statement_temporaries(program, statement.body, used);
    plan_statement_temporaries(program, statement.alternative, used);
  }
}

void plan_function_abis(lir::SemanticProgram& program) {
  for (const auto index : program.function_graph.definition_order) {
    auto& statement = program.statements[index];
    auto& abi = statement.function_abi;
    abi.valid = true;
    abi.recursive = program.function_graph.recursive[index];
    std::string explicit_type;
    const auto explicit_return = explicit_return_type(statement, explicit_type);
    abi.forward_declarable = abi.recursive && explicit_return;
    abi.return_type = abi.forward_declarable ? std::move(explicit_type) : "auto";
    abi.parameters.reserve(statement.parameters.size());
    for (std::size_t parameter = 0; parameter < statement.parameters.size(); ++parameter) {
      lir::ParameterAbi parameter_abi;
      parameter_abi.concrete_type = cpp_parameter_type(statement, parameter);
      const auto optional = parameter < statement.parameter_optional.size() &&
                            statement.parameter_optional[parameter];
      const auto intent = parameter < statement.parameter_intents.size()
                              ? statement.parameter_intents[parameter]
                              : ParameterIntent::none;
      if (optional) {
        parameter_abi.passing = lir::ParameterPassing::optional_reference;
      } else {
        parameter_abi.template_parameter = "T" + std::to_string(parameter);
        if (intent == ParameterIntent::in) {
          parameter_abi.passing = lir::ParameterPassing::const_reference;
        } else if (intent == ParameterIntent::out || intent == ParameterIntent::inout) {
          parameter_abi.passing = lir::ParameterPassing::mutable_reference;
        } else {
          parameter_abi.passing = lir::ParameterPassing::value;
        }
      }
      abi.parameters.push_back(std::move(parameter_abi));
    }
  }
}

void plan_target_resources(lir::SemanticProgram& program) {
  program.temporaries.offsets = {0};
  program.temporaries.slots.clear();
  auto used = program.identifiers.used;
  plan_function_abis(program);
  plan_statement_temporaries(program, program.statements, used);
  while (program.temporaries.offsets.size() <= program.node_count + 1U) {
    program.temporaries.offsets.push_back(
        static_cast<std::uint32_t>(program.temporaries.slots.size()));
  }
}

void require_temporary(const lir::SemanticProgram& program, const LirNodeId node,
                       const lir::TemporaryRole role, const std::size_t ordinal,
                       std::vector<std::size_t>& expected, std::set<std::string>& names,
                       std::vector<Diagnostic>& diagnostics, const SourceLocation location) {
  if (!node.valid() || node.value() >= expected.size()) {
    add_error(diagnostics, location, "cpp LIR temporary plan references an invalid node");
    return;
  }
  ++expected[node.value()];
  const auto* name = program.temporaries.find(node, role, ordinal);
  if (name == nullptr || name->empty()) {
    add_error(diagnostics, location, "cpp LIR temporary plan is incomplete");
  } else if (program.identifiers.used.count(*name) != 0U || !names.insert(*name).second) {
    add_error(diagnostics, location, "cpp LIR temporary name is not globally unique");
  }
}

void verify_expression_resources(const lir::SemanticProgram& program,
                                 const lir::Expression& expression,
                                 std::vector<std::size_t>& expected, std::set<std::string>& names,
                                 std::vector<Diagnostic>& diagnostics) {
  if (!expression.valid()) return;
  if (expression.kind == ExpressionKind::comparison_chain) {
    for (std::size_t index = 0; index < expression.children.size(); ++index) {
      require_temporary(program, expression.id, lir::TemporaryRole::comparison_operand, index,
                        expected, names, diagnostics, expression.location);
    }
  }
  if (expression.kind == ExpressionKind::call) {
    bool copied = false;
    for (std::size_t index = 0; index < expression.argument_transfers.size(); ++index) {
      if (!argument_transfer_copies(expression.argument_transfers[index])) continue;
      copied = true;
      require_temporary(program, expression.id, lir::TemporaryRole::section_argument, index,
                        expected, names, diagnostics, expression.location);
    }
    if (copied && expression.procedure_has_result) {
      require_temporary(program, expression.id, lir::TemporaryRole::call_result, 0, expected, names,
                        diagnostics, expression.location);
    }
  }
  for (const auto& child : expression.children) {
    verify_expression_resources(program, child, expected, names, diagnostics);
  }
}

void verify_function_abi(const lir::SemanticProgram& program, const lir::Statement& statement,
                         const std::size_t index, std::vector<Diagnostic>& diagnostics) {
  const auto& abi = statement.function_abi;
  if (!abi.valid || abi.parameters.size() != statement.parameters.size() ||
      index >= program.function_graph.recursive.size() ||
      abi.recursive != program.function_graph.recursive[index]) {
    add_error(diagnostics, {statement.line, 1}, "cpp LIR function ABI inventory is inconsistent");
    return;
  }
  std::string explicit_type;
  const auto can_declare = abi.recursive && explicit_return_type(statement, explicit_type);
  if (abi.forward_declarable != can_declare ||
      abi.return_type != (can_declare ? explicit_type : "auto")) {
    add_error(diagnostics, {statement.line, 1}, "cpp LIR recursive return ABI is inconsistent");
  }
  for (std::size_t parameter = 0; parameter < abi.parameters.size(); ++parameter) {
    const auto& actual = abi.parameters[parameter];
    const auto optional =
        parameter < statement.parameter_optional.size() && statement.parameter_optional[parameter];
    const auto intent = parameter < statement.parameter_intents.size()
                            ? statement.parameter_intents[parameter]
                            : ParameterIntent::none;
    auto expected = lir::ParameterPassing::value;
    if (optional) {
      expected = lir::ParameterPassing::optional_reference;
    } else if (intent == ParameterIntent::in) {
      expected = lir::ParameterPassing::const_reference;
    } else if (intent == ParameterIntent::out || intent == ParameterIntent::inout) {
      expected = lir::ParameterPassing::mutable_reference;
    }
    if (actual.passing != expected ||
        actual.concrete_type != cpp_parameter_type(statement, parameter) ||
        (optional && !actual.template_parameter.empty()) ||
        (!optional && actual.template_parameter != "T" + std::to_string(parameter))) {
      add_error(diagnostics, {statement.line, 1}, "cpp LIR parameter passing ABI is inconsistent");
    }
  }
}

void verify_statement_resources(const lir::SemanticProgram& program,
                                const std::vector<lir::Statement>& statements,
                                std::vector<std::size_t>& expected, std::set<std::string>& names,
                                std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::function && !statement.function_abi.valid) {
      add_error(diagnostics, {statement.line, 1}, "cpp LIR function is missing its ABI plan");
    }
    if (statement.kind != StatementKind::function &&
        (statement.function_abi.valid || !statement.function_abi.parameters.empty())) {
      add_error(diagnostics, {statement.line, 1}, "non-function cpp LIR node owns function ABI");
    }
    if (statement.kind == StatementKind::select_case) {
      require_temporary(program, statement.id, lir::TemporaryRole::select_value, 0, expected, names,
                        diagnostics, {statement.line, 1});
    } else if (statement.kind == StatementKind::multi_assignment) {
      require_temporary(program, statement.id, lir::TemporaryRole::assignment_value, 0, expected,
                        names, diagnostics, {statement.line, 1});
    } else if (statement.kind == StatementKind::while_loop && !statement.alternative.empty()) {
      require_temporary(program, statement.id, lir::TemporaryRole::loop_completed, 0, expected,
                        names, diagnostics, {statement.line, 1});
    } else if (statement.kind == StatementKind::range_loop) {
      require_temporary(program, statement.id, lir::TemporaryRole::range_start, 0, expected, names,
                        diagnostics, {statement.line, 1});
      require_temporary(program, statement.id, lir::TemporaryRole::range_stop, 0, expected, names,
                        diagnostics, {statement.line, 1});
      require_temporary(program, statement.id, lir::TemporaryRole::range_step, 0, expected, names,
                        diagnostics, {statement.line, 1});
      require_temporary(program, statement.id, lir::TemporaryRole::range_first, 0, expected, names,
                        diagnostics, {statement.line, 1});
      if (statement.retain_last_loop_value) {
        require_temporary(program, statement.id, lir::TemporaryRole::range_cursor, 0, expected,
                          names, diagnostics, {statement.line, 1});
      }
      if (!statement.alternative.empty()) {
        require_temporary(program, statement.id, lir::TemporaryRole::loop_completed, 0, expected,
                          names, diagnostics, {statement.line, 1});
      }
    }
    verify_expression_resources(program, statement.expression, expected, names, diagnostics);
    verify_expression_resources(program, statement.secondary_expression, expected, names,
                                diagnostics);
    verify_expression_resources(program, statement.tertiary_expression, expected, names,
                                diagnostics);
    verify_expression_resources(program, statement.target_expression, expected, names, diagnostics);
    for (const auto& expression : statement.parameter_defaults) {
      verify_expression_resources(program, expression, expected, names, diagnostics);
    }
    for (const auto& selector : statement.case_selectors) {
      verify_expression_resources(program, selector.lower, expected, names, diagnostics);
      verify_expression_resources(program, selector.upper, expected, names, diagnostics);
    }
    verify_statement_resources(program, statement.body, expected, names, diagnostics);
    verify_statement_resources(program, statement.alternative, expected, names, diagnostics);
  }
}

void verify_target_resources(const lir::SemanticProgram& program,
                             std::vector<Diagnostic>& diagnostics) {
  if (program.temporaries.offsets.size() != program.node_count + 2U ||
      program.temporaries.offsets.back() != program.temporaries.slots.size()) {
    add_error(diagnostics, {1, 1}, "cpp LIR temporary plan has invalid dense inventory");
    return;
  }
  for (const auto index : program.function_graph.definition_order) {
    if (index >= program.statements.size() ||
        program.statements[index].kind != StatementKind::function) {
      add_error(diagnostics, {1, 1}, "cpp LIR function graph contains an invalid definition");
      continue;
    }
    verify_function_abi(program, program.statements[index], index, diagnostics);
  }
  std::vector<std::size_t> expected(program.node_count + 1U);
  std::set<std::string> names;
  verify_statement_resources(program, program.statements, expected, names, diagnostics);
  for (std::size_t node = 0; node <= program.node_count; ++node) {
    if (program.temporaries.offsets[node] > program.temporaries.offsets[node + 1U] ||
        program.temporaries.offsets[node + 1U] - program.temporaries.offsets[node] !=
            expected[node]) {
      add_error(diagnostics, {1, 1}, "cpp LIR temporary plan contains unexpected slots");
    }
  }
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
  verify_target_resources(program, diagnostics);
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
  plan_target_resources(*lowered);
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
    artifact->chunks = materialize_chunks(render_cpp(*lowered, options));
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
      }
      self(self, statement.body);
      self(self, statement.alternative);
    }
  };
  dump_abis(dump_abis, program.statements);
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
