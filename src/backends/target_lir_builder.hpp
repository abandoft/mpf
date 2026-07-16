#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "../ir/ids.hpp"
#include "../ir/mir.hpp"

namespace mpf::detail {

template <typename LirExpression, typename ResolveBinding>
LirExpression lower_lir_expression(const mir::Expression& source, IrIdAllocator<LirNodeId>& ids,
                                   const ResolveBinding& resolve_binding,
                                   const std::vector<const mir::CallSite*>& call_sites) {
  LirExpression result;
  if (source.valid()) result.id = ids.next();
  result.origin = source.origin;
  result.location = source.location;
  result.kind = source.kind;
  result.value = source.value;
  result.comparison = source.comparison;
  result.comparisons = source.comparisons;
  result.children.reserve(source.children.size());
  for (const auto& child : source.children) {
    result.children.push_back(
        lower_lir_expression<LirExpression>(child, ids, resolve_binding, call_sites));
  }
  result.inferred_type = source.inferred_type;
  result.binding = source.binding;
  result.intrinsic = source.intrinsic;
  if (source.binding == BindingKind::builtin && source.intrinsic != IntrinsicId::none) {
    result.target_binding = resolve_binding(source.origin, source.intrinsic);
  }
  result.element_type = source.element_type;
  result.shape = source.shape;
  result.tuple_types = source.tuple_types;
  result.tuple_element_types = source.tuple_element_types;
  result.tuple_shapes = source.tuple_shapes;
  result.sequence_is_list = source.sequence_is_list;
  result.sequence_elements = source.sequence_elements;
  result.requested_outputs = source.requested_outputs;
  result.multi_output_call = source.multi_output_call;
  if (source.kind == ExpressionKind::call && source.origin.valid() &&
      source.origin.value() < call_sites.size()) {
    const auto* call = call_sites[source.origin.value()];
    if (call != nullptr) {
      result.argument_transfers.reserve(call->arguments.size());
      for (const auto& argument : call->arguments) {
        result.argument_transfers.push_back(argument.transfer);
      }
    }
  }
  result.argument_names = source.argument_names;
  result.procedure_has_result = source.procedure_has_result;
  result.index_base = source.index_base;
  result.allow_negative_index = source.allow_negative_index;
  result.column_major = source.column_major;
  result.slice_stop_inclusive = source.slice_stop_inclusive;
  return result;
}

template <typename LirSelector, typename LirExpression, typename ResolveBinding>
LirSelector lower_lir_selector(const mir::CaseSelector& source, IrIdAllocator<LirNodeId>& ids,
                               const ResolveBinding& resolve_binding,
                               const std::vector<const mir::CallSite*>& call_sites) {
  LirSelector result;
  result.lower =
      lower_lir_expression<LirExpression>(source.lower, ids, resolve_binding, call_sites);
  result.has_lower = source.has_lower;
  result.upper =
      lower_lir_expression<LirExpression>(source.upper, ids, resolve_binding, call_sites);
  result.has_upper = source.has_upper;
  result.range = source.range;
  return result;
}

template <typename LirStatement, typename LirExpression, typename LirSelector,
          typename ResolveBinding>
LirStatement lower_lir_statement(const mir::Statement& source, IrIdAllocator<LirNodeId>& ids,
                                 const ResolveBinding& resolve_binding,
                                 const std::vector<const mir::CallSite*>& call_sites) {
  LirStatement result;
  result.id = ids.next();
  result.origin = source.origin;
  result.kind = source.kind;
  result.line = source.line;
  result.name = source.name;
  result.expression =
      lower_lir_expression<LirExpression>(source.expression, ids, resolve_binding, call_sites);
  result.has_expression = source.has_expression;
  result.procedure_call = source.procedure_call;
  result.secondary_expression = lower_lir_expression<LirExpression>(
      source.secondary_expression, ids, resolve_binding, call_sites);
  result.has_secondary_expression = source.has_secondary_expression;
  result.tertiary_expression = lower_lir_expression<LirExpression>(source.tertiary_expression, ids,
                                                                   resolve_binding, call_sites);
  result.has_tertiary_expression = source.has_tertiary_expression;
  result.inclusive_stop = source.inclusive_stop;
  result.retain_last_loop_value = source.retain_last_loop_value;
  result.declared_type = source.declared_type;
  result.element_type = source.element_type;
  result.previous_type = source.previous_type;
  result.previous_element_type = source.previous_element_type;
  result.parameter_intent = source.parameter_intent;
  result.optional_parameter = source.optional_parameter;
  result.dummy_parameter = source.dummy_parameter;
  result.shape = source.shape;
  result.index_base = source.index_base;
  result.allow_negative_index = source.allow_negative_index;
  result.target_expression = lower_lir_expression<LirExpression>(source.target_expression, ids,
                                                                 resolve_binding, call_sites);
  result.has_target_expression = source.has_target_expression;
  result.parameters = source.parameters;
  result.parameter_kinds = source.parameter_kinds;
  result.parameter_defaults.reserve(source.parameter_defaults.size());
  for (const auto& expression : source.parameter_defaults) {
    result.parameter_defaults.push_back(
        lower_lir_expression<LirExpression>(expression, ids, resolve_binding, call_sites));
  }
  result.parameter_intents = source.parameter_intents;
  result.parameter_optional = source.parameter_optional;
  result.parameter_types = source.parameter_types;
  result.parameter_element_types = source.parameter_element_types;
  result.parameter_shapes = source.parameter_shapes;
  result.return_names = source.return_names;
  result.has_value_return = source.has_value_return;
  result.return_types = source.return_types;
  result.return_element_types = source.return_element_types;
  result.return_shapes = source.return_shapes;
  result.return_sequence_is_list = source.return_sequence_is_list;
  result.return_sequence_elements = source.return_sequence_elements;
  result.target_names = source.target_names;
  result.target_pattern = source.target_pattern;
  result.has_target_pattern = source.has_target_pattern;
  result.target_types = source.target_types;
  result.target_element_types = source.target_element_types;
  result.target_shapes = source.target_shapes;
  result.target_previous_types = source.target_previous_types;
  result.target_previous_element_types = source.target_previous_element_types;
  result.case_selectors.reserve(source.case_selectors.size());
  for (const auto& selector : source.case_selectors) {
    result.case_selectors.push_back(
        lower_lir_selector<LirSelector, LirExpression>(selector, ids, resolve_binding, call_sites));
  }
  result.default_case = source.default_case;
  result.body.reserve(source.body.size());
  for (const auto& statement : source.body) {
    result.body.push_back(lower_lir_statement<LirStatement, LirExpression, LirSelector>(
        statement, ids, resolve_binding, call_sites));
  }
  result.alternative.reserve(source.alternative.size());
  for (const auto& statement : source.alternative) {
    result.alternative.push_back(lower_lir_statement<LirStatement, LirExpression, LirSelector>(
        statement, ids, resolve_binding, call_sites));
  }
  return result;
}

template <typename LirProgram, typename LirStatement, typename LirExpression, typename LirSelector,
          typename ResolveBinding>
std::unique_ptr<LirProgram> lower_structured_lir(const mir::Program& source,
                                                 const ResolveBinding& resolve_binding) {
  auto result = std::make_unique<LirProgram>();
  result->source_language = source.source_language;
  std::vector<const mir::CallSite*> call_sites(source.hir_node_count + 1U);
  for (const auto& call : source.calls) {
    if (call.origin.valid() && call.origin.value() < call_sites.size()) {
      call_sites[call.origin.value()] = &call;
    }
  }
  IrIdAllocator<LirNodeId> ids;
  result->statements.reserve(source.statements.size());
  for (const auto& statement : source.statements) {
    result->statements.push_back(lower_lir_statement<LirStatement, LirExpression, LirSelector>(
        statement, ids, resolve_binding, call_sites));
  }
  result->node_count = ids.count();
  return result;
}

}  // namespace mpf::detail
