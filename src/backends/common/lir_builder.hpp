#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "ir/ids.hpp"
#include "ir/mir.hpp"

namespace mpf::detail {

inline ValueMetadata lower_value_metadata(const mir::Program& program,
                                          const mir::ValueMetadata& source) {
  ValueMetadata result;
  result.type = mir::value_type(program, source.type);
  result.element_type = mir::element_type(program, source.type);
  result.numeric_type = mir::numeric_type(program, source.type);
  result.element_numeric_type = mir::element_numeric_type(program, source.type);
  result.array_storage = mir::array_storage(program, source.type);
  const auto* source_shape = mir::shape(program, source.shape);
  if (source_shape != nullptr) result.shape = source_shape->extents;
  result.sequence = source.sequence;
  result.list_sequence = source.list_sequence;
  result.elements.reserve(source.elements.size());
  for (const auto& element : source.elements) {
    result.elements.push_back(lower_value_metadata(program, element));
  }
  return result;
}

inline AssignmentPattern lower_assignment_pattern(const mir::Program& program,
                                                  const mir::AssignmentPattern& source) {
  AssignmentPattern result;
  result.kind = source.kind;
  result.location = source.location;
  result.name = source.name;
  result.type = mir::value_type(program, source.type);
  result.element_type = mir::element_type(program, source.type);
  result.numeric_type = mir::numeric_type(program, source.type);
  result.element_numeric_type = mir::element_numeric_type(program, source.type);
  result.array_storage = mir::array_storage(program, source.type);
  const auto* source_shape = mir::shape(program, source.shape);
  if (source_shape != nullptr) result.shape = source_shape->extents;
  result.previous_type = mir::value_type(program, source.previous_type);
  result.previous_element_type = mir::element_type(program, source.previous_type);
  result.previous_numeric_type = mir::numeric_type(program, source.previous_type);
  result.previous_element_numeric_type = mir::element_numeric_type(program, source.previous_type);
  result.previous_array_storage = mir::array_storage(program, source.previous_type);
  result.access_path = source.access_path;
  result.captured_paths = source.captured_paths;
  result.children.reserve(source.children.size());
  for (const auto& child : source.children) {
    result.children.push_back(lower_assignment_pattern(program, child));
  }
  return result;
}

inline const mir::Function* function_for_origin(const mir::Program& program,
                                                const HirNodeId origin) noexcept {
  for (std::size_t index = 1; index < program.functions.size(); ++index) {
    if (program.functions[index].origin == origin) return &program.functions[index];
  }
  return nullptr;
}

template <typename LirExpression, typename ResolveBinding>
LirExpression lower_lir_expression(const mir::Program& program, const MirExpressionId source_id,
                                   IrIdAllocator<LirNodeId>& ids,
                                   const ResolveBinding& resolve_binding,
                                   const std::vector<const mir::CallSite*>& call_sites) {
  LirExpression result;
  const auto* source_node = mir::expression(program, source_id);
  const auto* source_attributes = mir::attributes(program, source_id);
  if (source_node == nullptr || source_attributes == nullptr) return result;
  const auto& source = *source_node;
  const auto& attributes = *source_attributes;
  if (source.valid()) result.id = ids.next();
  result.origin = source.origin;
  result.location = source.location;
  result.kind = source.kind;
  result.value = attributes.spelling;
  result.unary_operation = attributes.unary_operation;
  result.symbol_id = source.symbol_id;
  result.operation = attributes.operation;
  result.comparison = attributes.comparison;
  result.comparisons = attributes.comparisons;
  result.children.reserve(source.children.size());
  for (const auto& child : source.children) {
    result.children.push_back(
        lower_lir_expression<LirExpression>(program, child, ids, resolve_binding, call_sites));
  }
  result.inferred_type = mir::value_type(program, source.type_id);
  result.numeric_type = mir::numeric_type(program, source.type_id);
  result.binding = attributes.binding;
  result.intrinsic = attributes.intrinsic;
  if (attributes.binding == BindingKind::builtin && attributes.intrinsic != IntrinsicId::none) {
    result.target_binding = resolve_binding(source.origin, attributes.intrinsic);
  }
  result.element_type = mir::element_type(program, source.type_id);
  result.element_numeric_type = mir::element_numeric_type(program, source.type_id);
  result.array_storage = mir::array_storage(program, source.type_id);
  const auto* source_shape = mir::shape(program, source.shape_id);
  if (source_shape != nullptr) result.shape = source_shape->extents;
  result.logical_evaluation = attributes.logical_evaluation;
  result.array_operation = attributes.array_operation;
  if (attributes.broadcast.valid) {
    result.broadcast.valid = true;
    result.broadcast.shape_source = attributes.broadcast.shape_source;
    const auto* left_shape = mir::shape(program, attributes.broadcast.left_shape);
    const auto* right_shape = mir::shape(program, attributes.broadcast.right_shape);
    const auto* result_shape = mir::shape(program, attributes.broadcast.result_shape);
    if (left_shape != nullptr) result.broadcast.left_shape = left_shape->extents;
    if (right_shape != nullptr) result.broadcast.right_shape = right_shape->extents;
    if (result_shape != nullptr) result.broadcast.result_shape = result_shape->extents;
    result.broadcast.axes = attributes.broadcast.axes;
  }
  if (attributes.sparse_arithmetic.valid()) {
    result.sparse_arithmetic.operation = attributes.sparse_arithmetic.operation;
    result.sparse_arithmetic.storage_policy = attributes.sparse_arithmetic.storage_policy;
    result.sparse_arithmetic.value_domain = attributes.sparse_arithmetic.value_domain;
    result.sparse_arithmetic.shape_source = attributes.sparse_arithmetic.shape_source;
    result.sparse_arithmetic.left_storage = attributes.sparse_arithmetic.left_storage;
    result.sparse_arithmetic.right_storage = attributes.sparse_arithmetic.right_storage;
    result.sparse_arithmetic.result_storage = attributes.sparse_arithmetic.result_storage;
    const auto* left_shape = mir::shape(program, attributes.sparse_arithmetic.left_shape);
    const auto* right_shape = mir::shape(program, attributes.sparse_arithmetic.right_shape);
    const auto* result_shape = mir::shape(program, attributes.sparse_arithmetic.result_shape);
    if (left_shape != nullptr) result.sparse_arithmetic.left_shape = left_shape->extents;
    if (right_shape != nullptr) result.sparse_arithmetic.right_shape = right_shape->extents;
    if (result_shape != nullptr) result.sparse_arithmetic.result_shape = result_shape->extents;
    result.sparse_arithmetic.axes = attributes.sparse_arithmetic.axes;
  }
  if (attributes.sparse_elementwise.valid()) {
    result.sparse_elementwise.operation = attributes.sparse_elementwise.operation;
    result.sparse_elementwise.storage_policy = attributes.sparse_elementwise.storage_policy;
    result.sparse_elementwise.shape_source = attributes.sparse_elementwise.shape_source;
    result.sparse_elementwise.left_storage = attributes.sparse_elementwise.left_storage;
    result.sparse_elementwise.right_storage = attributes.sparse_elementwise.right_storage;
    result.sparse_elementwise.result_storage = attributes.sparse_elementwise.result_storage;
    const auto* left_shape = mir::shape(program, attributes.sparse_elementwise.left_shape);
    const auto* right_shape = mir::shape(program, attributes.sparse_elementwise.right_shape);
    const auto* result_shape = mir::shape(program, attributes.sparse_elementwise.result_shape);
    if (left_shape != nullptr) result.sparse_elementwise.left_shape = left_shape->extents;
    if (right_shape != nullptr) result.sparse_elementwise.right_shape = right_shape->extents;
    if (result_shape != nullptr) result.sparse_elementwise.result_shape = result_shape->extents;
    result.sparse_elementwise.axes = attributes.sparse_elementwise.axes;
  }
  if (attributes.sparse_logical.valid()) {
    result.sparse_logical.operation = attributes.sparse_logical.operation;
    result.sparse_logical.storage_policy = attributes.sparse_logical.storage_policy;
    result.sparse_logical.shape_source = attributes.sparse_logical.shape_source;
    result.sparse_logical.left_storage = attributes.sparse_logical.left_storage;
    result.sparse_logical.right_storage = attributes.sparse_logical.right_storage;
    result.sparse_logical.result_storage = attributes.sparse_logical.result_storage;
    const auto* left_shape = mir::shape(program, attributes.sparse_logical.left_shape);
    const auto* right_shape = mir::shape(program, attributes.sparse_logical.right_shape);
    const auto* result_shape = mir::shape(program, attributes.sparse_logical.result_shape);
    if (left_shape != nullptr) result.sparse_logical.left_shape = left_shape->extents;
    if (right_shape != nullptr) result.sparse_logical.right_shape = right_shape->extents;
    if (result_shape != nullptr) result.sparse_logical.result_shape = result_shape->extents;
    result.sparse_logical.axes = attributes.sparse_logical.axes;
  }
  if (attributes.matrix_operation.valid()) {
    result.matrix_operation.operation = attributes.matrix_operation.operation;
    result.matrix_operation.solve = attributes.matrix_operation.solve;
    result.matrix_operation.numeric_domain = attributes.matrix_operation.numeric_domain;
    result.matrix_operation.condition_policy = attributes.matrix_operation.condition_policy;
    result.matrix_operation.factorization_policy = attributes.matrix_operation.factorization_policy;
    result.matrix_operation.structure_policy = attributes.matrix_operation.structure_policy;
    result.matrix_operation.storage_policy = attributes.matrix_operation.storage_policy;
    result.matrix_operation.exponent_policy = attributes.matrix_operation.exponent_policy;
    result.matrix_operation.left_storage = attributes.matrix_operation.left_storage;
    result.matrix_operation.right_storage = attributes.matrix_operation.right_storage;
    result.matrix_operation.result_storage = attributes.matrix_operation.result_storage;
    const auto* left_shape = mir::shape(program, attributes.matrix_operation.left_shape);
    const auto* right_shape = mir::shape(program, attributes.matrix_operation.right_shape);
    const auto* result_shape = mir::shape(program, attributes.matrix_operation.result_shape);
    if (left_shape != nullptr) result.matrix_operation.left_shape = left_shape->extents;
    if (right_shape != nullptr) result.matrix_operation.right_shape = right_shape->extents;
    if (result_shape != nullptr) result.matrix_operation.result_shape = result_shape->extents;
  }
  if (attributes.sparse_construction.valid()) {
    result.sparse_construction.kind = attributes.sparse_construction.kind;
    const auto* result_shape = mir::shape(program, attributes.sparse_construction.result_shape);
    if (result_shape != nullptr) result.sparse_construction.result_shape = result_shape->extents;
    result.sparse_construction.triplet_element_counts =
        attributes.sparse_construction.triplet_element_counts;
    result.sparse_construction.reserve_hint = attributes.sparse_construction.reserve_hint;
    result.sparse_construction.value_domain = attributes.sparse_construction.value_domain;
    result.sparse_construction.duplicate_policy = attributes.sparse_construction.duplicate_policy;
  }
  if (attributes.sparse_index.valid()) {
    result.sparse_index.kind = attributes.sparse_index.kind;
    result.sparse_index.source_storage = attributes.sparse_index.source_storage;
    result.sparse_index.result_storage = attributes.sparse_index.result_storage;
    const auto* input_shape = mir::shape(program, attributes.sparse_index.input_shape);
    const auto* result_shape = mir::shape(program, attributes.sparse_index.result_shape);
    if (input_shape != nullptr) result.sparse_index.input_shape = input_shape->extents;
    if (result_shape != nullptr) result.sparse_index.result_shape = result_shape->extents;
  }
  if (attributes.sparse_reshape.valid()) {
    result.sparse_reshape.kind = attributes.sparse_reshape.kind;
    result.sparse_reshape.dimension_form = attributes.sparse_reshape.dimension_form;
    result.sparse_reshape.inference = attributes.sparse_reshape.inference;
    result.sparse_reshape.inferred_axis = attributes.sparse_reshape.inferred_axis;
    result.sparse_reshape.source_storage = attributes.sparse_reshape.source_storage;
    result.sparse_reshape.result_storage = attributes.sparse_reshape.result_storage;
    const auto* input_shape = mir::shape(program, attributes.sparse_reshape.input_shape);
    const auto* requested_shape = mir::shape(program, attributes.sparse_reshape.requested_shape);
    const auto* result_shape = mir::shape(program, attributes.sparse_reshape.result_shape);
    if (input_shape != nullptr) result.sparse_reshape.input_shape = input_shape->extents;
    if (requested_shape != nullptr) {
      result.sparse_reshape.requested_shape = requested_shape->extents;
    }
    if (result_shape != nullptr) result.sparse_reshape.result_shape = result_shape->extents;
  }
  if (attributes.reduction.valid()) {
    result.reduction.operation = attributes.reduction.operation;
    result.reduction.axis_policy = attributes.reduction.axis_policy;
    result.reduction.shape_source = attributes.reduction.shape_source;
    const auto* input_shape = mir::shape(program, attributes.reduction.input_shape);
    const auto* reduction_result_shape = mir::shape(program, attributes.reduction.result_shape);
    const auto* output_shape = mir::shape(program, attributes.reduction.output_shape);
    if (input_shape != nullptr) result.reduction.input_shape = input_shape->extents;
    if (reduction_result_shape != nullptr) {
      result.reduction.result_shape = reduction_result_shape->extents;
    }
    if (output_shape != nullptr) result.reduction.output_shape = output_shape->extents;
    result.reduction.axes = attributes.reduction.axes;
    result.reduction.scalar_result = attributes.reduction.scalar_result;
    result.reduction.storage_policy = attributes.reduction.storage_policy;
    result.reduction.input_storage = attributes.reduction.input_storage;
    result.reduction.result_storage = attributes.reduction.result_storage;
  }
  const auto* source_type = mir::type(program, source.type_id);
  if (source_type != nullptr && source_type->kind == mir::TypeKind::tuple) {
    result.tuple_types.reserve(source_type->elements.size());
    result.tuple_numeric_types.reserve(source_type->elements.size());
    result.tuple_element_types.reserve(source_type->elements.size());
    result.tuple_element_numeric_types.reserve(source_type->elements.size());
    result.tuple_array_storage.reserve(source_type->elements.size());
    for (const auto element : source_type->elements) {
      result.tuple_types.push_back(mir::value_type(program, element));
      result.tuple_numeric_types.push_back(mir::numeric_type(program, element));
      result.tuple_element_types.push_back(mir::element_type(program, element));
      result.tuple_element_numeric_types.push_back(mir::element_numeric_type(program, element));
      result.tuple_array_storage.push_back(mir::array_storage(program, element));
    }
  }
  result.tuple_shapes.reserve(attributes.tuple_shapes.size());
  for (const auto tuple_shape : attributes.tuple_shapes) {
    const auto* shape = mir::shape(program, tuple_shape);
    result.tuple_shapes.push_back(shape == nullptr ? std::vector<std::size_t>{} : shape->extents);
  }
  result.sequence_is_list = result.inferred_type == ValueType::list;
  result.sequence_elements.reserve(attributes.sequence_elements.size());
  for (const auto& element : attributes.sequence_elements) {
    result.sequence_elements.push_back(lower_value_metadata(program, element));
  }
  result.requested_outputs = attributes.requested_results;
  result.multi_output_call = attributes.multi_result_call;
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
  result.procedure_has_result = attributes.procedure_has_result;
  result.index_base = attributes.index_base;
  result.allow_negative_index = attributes.allow_negative_index;
  result.column_major = mir::column_major(program, source.shape_id);
  result.slice_stop_inclusive = attributes.slice_stop_inclusive;
  result.index_extent = attributes.index_extent;
  result.index_selectors = attributes.index_selectors;
  result.index_extents = attributes.index_extents;
  return result;
}

template <typename LirSelector, typename LirExpression, typename ResolveBinding>
LirSelector lower_lir_selector(const mir::CaseSelector& source, IrIdAllocator<LirNodeId>& ids,
                               const mir::Program& program, const ResolveBinding& resolve_binding,
                               const std::vector<const mir::CallSite*>& call_sites) {
  LirSelector result;
  result.lower =
      lower_lir_expression<LirExpression>(program, source.lower, ids, resolve_binding, call_sites);
  result.has_lower = source.has_lower;
  result.upper =
      lower_lir_expression<LirExpression>(program, source.upper, ids, resolve_binding, call_sites);
  result.has_upper = source.has_upper;
  result.range = source.range;
  return result;
}

template <typename LirStatement, typename LirExpression, typename LirSelector,
          typename ResolveBinding>
LirStatement lower_lir_statement(const mir::Program& program, const MirStatementId source_id,
                                 IrIdAllocator<LirNodeId>& ids,
                                 const ResolveBinding& resolve_binding,
                                 const std::vector<const mir::CallSite*>& call_sites) {
  LirStatement result;
  const auto* source_node = mir::statement(program, source_id);
  const auto* source_attributes = mir::attributes(program, source_id);
  if (source_node == nullptr || source_attributes == nullptr) return result;
  const auto& source = *source_node;
  const auto& attributes = *source_attributes;
  const auto* instruction =
      source.instruction.valid() && source.instruction.value() < program.instructions.size()
          ? &program.instructions[source.instruction.value()]
          : nullptr;
  const auto* storage = instruction != nullptr && instruction->storage.valid() &&
                                instruction->storage.value() < program.storages.size()
                            ? &program.storages[instruction->storage.value()]
                            : nullptr;
  const auto* function = function_for_origin(program, source.origin);
  result.id = ids.next();
  result.origin = source.origin;
  result.kind = source.kind;
  result.line = source.line;
  result.name = source.name;
  result.symbol_id = source.symbol_id;
  result.expression = lower_lir_expression<LirExpression>(program, source.expression, ids,
                                                          resolve_binding, call_sites);
  result.has_expression = source.has_expression;
  result.procedure_call = attributes.procedure_call;
  result.implicit_result = attributes.implicit_result;
  result.implicit_result_has_value = attributes.implicit_result_has_value;
  result.previous_assigned = attributes.previous_assigned;
  result.secondary_expression = lower_lir_expression<LirExpression>(
      program, source.secondary_expression, ids, resolve_binding, call_sites);
  result.has_secondary_expression = source.has_secondary_expression;
  result.tertiary_expression = lower_lir_expression<LirExpression>(
      program, source.tertiary_expression, ids, resolve_binding, call_sites);
  result.has_tertiary_expression = source.has_tertiary_expression;
  result.inclusive_stop = attributes.inclusive_stop;
  result.retain_last_loop_value = attributes.retain_last_loop_value;
  result.source_exported = function != nullptr && function->exported;
  if (storage != nullptr) {
    result.declared_type = mir::value_type(program, storage->type);
    result.declared_numeric_type = mir::numeric_type(program, storage->type);
    result.element_type = mir::element_type(program, storage->type);
    result.element_numeric_type = mir::element_numeric_type(program, storage->type);
    result.array_storage = mir::array_storage(program, storage->type);
    const auto* storage_shape = mir::shape(program, storage->shape);
    if (storage_shape != nullptr) result.shape = storage_shape->extents;
    result.parameter_intent = storage->intent;
    result.optional_parameter = storage->optional;
    result.dummy_parameter = storage->kind == mir::StorageKind::parameter;
  } else if (function != nullptr && function->result_types.size() == 1U) {
    result.declared_type = mir::value_type(program, function->result_types.front());
    result.declared_numeric_type = mir::numeric_type(program, function->result_types.front());
    result.element_type = mir::element_type(program, function->result_types.front());
    result.element_numeric_type =
        mir::element_numeric_type(program, function->result_types.front());
    result.array_storage = mir::array_storage(program, function->result_types.front());
    if (!function->result_shapes.empty()) {
      const auto* result_shape = mir::shape(program, function->result_shapes.front());
      if (result_shape != nullptr) result.shape = result_shape->extents;
    }
  }
  result.previous_type = mir::value_type(program, attributes.previous_type);
  result.previous_element_type = mir::element_type(program, attributes.previous_type);
  result.previous_numeric_type = mir::numeric_type(program, attributes.previous_type);
  result.previous_element_numeric_type =
      mir::element_numeric_type(program, attributes.previous_type);
  result.previous_array_storage = mir::array_storage(program, attributes.previous_type);
  result.target_expression = lower_lir_expression<LirExpression>(program, source.target_expression,
                                                                 ids, resolve_binding, call_sites);
  result.has_target_expression = source.has_target_expression;
  result.parameters = source.parameters;
  result.parameter_symbols = source.parameter_symbols;
  result.parameter_kinds = source.parameter_kinds;
  result.parameter_defaults.reserve(source.parameter_defaults.size());
  for (const auto expression : source.parameter_defaults) {
    result.parameter_defaults.push_back(
        lower_lir_expression<LirExpression>(program, expression, ids, resolve_binding, call_sites));
  }
  if (function != nullptr) {
    result.parameter_optional = function->parameter_optional;
    result.parameter_types.reserve(function->parameter_types.size());
    result.parameter_numeric_types.reserve(function->parameter_types.size());
    result.parameter_element_types.reserve(function->parameter_types.size());
    result.parameter_element_numeric_types.reserve(function->parameter_types.size());
    result.parameter_array_storage.reserve(function->parameter_types.size());
    result.parameter_shapes.reserve(function->parameter_shapes.size());
    const auto* entry = function->entry.valid() && function->entry.value() < program.blocks.size()
                            ? &program.blocks[function->entry.value()]
                            : nullptr;
    for (std::size_t index = 0; index < function->parameter_types.size(); ++index) {
      result.parameter_types.push_back(mir::value_type(program, function->parameter_types[index]));
      result.parameter_numeric_types.push_back(
          mir::numeric_type(program, function->parameter_types[index]));
      result.parameter_element_types.push_back(
          mir::element_type(program, function->parameter_types[index]));
      result.parameter_element_numeric_types.push_back(
          mir::element_numeric_type(program, function->parameter_types[index]));
      result.parameter_array_storage.push_back(
          mir::array_storage(program, function->parameter_types[index]));
      const auto* parameter_shape = index < function->parameter_shapes.size()
                                        ? mir::shape(program, function->parameter_shapes[index])
                                        : nullptr;
      result.parameter_shapes.push_back(parameter_shape == nullptr ? std::vector<std::size_t>{}
                                                                   : parameter_shape->extents);
      ParameterIntent intent = ParameterIntent::none;
      if (entry != nullptr && index < entry->arguments.size()) {
        const auto parameter_storage = entry->arguments[index].storage;
        if (parameter_storage.valid() && parameter_storage.value() < program.storages.size()) {
          intent = program.storages[parameter_storage.value()].intent;
        }
      }
      result.parameter_intents.push_back(intent);
    }
  }
  result.return_names = source.return_names;
  result.return_symbols = source.return_symbols;
  result.has_value_return = function != nullptr && !function->result_types.empty();
  if (function != nullptr) {
    const auto* tuple_result = function->result_types.size() == 1U
                                   ? mir::type(program, function->result_types.front())
                                   : nullptr;
    if (tuple_result != nullptr && tuple_result->kind == mir::TypeKind::tuple) {
      result.declared_type = ValueType::tuple;
      for (const auto element : tuple_result->elements) {
        result.return_types.push_back(mir::value_type(program, element));
        result.return_numeric_types.push_back(mir::numeric_type(program, element));
        result.return_element_types.push_back(mir::element_type(program, element));
        result.return_element_numeric_types.push_back(mir::element_numeric_type(program, element));
        result.return_array_storage.push_back(mir::array_storage(program, element));
        result.return_shapes.push_back({});
      }
    } else {
      result.return_types.reserve(function->result_types.size());
      result.return_numeric_types.reserve(function->result_types.size());
      result.return_element_types.reserve(function->result_types.size());
      result.return_element_numeric_types.reserve(function->result_types.size());
      result.return_array_storage.reserve(function->result_types.size());
      result.return_shapes.reserve(function->result_shapes.size());
      for (const auto type : function->result_types) {
        result.return_types.push_back(mir::value_type(program, type));
        result.return_numeric_types.push_back(mir::numeric_type(program, type));
        result.return_element_types.push_back(mir::element_type(program, type));
        result.return_element_numeric_types.push_back(mir::element_numeric_type(program, type));
        result.return_array_storage.push_back(mir::array_storage(program, type));
      }
      for (const auto shape : function->result_shapes) {
        const auto* data = mir::shape(program, shape);
        result.return_shapes.push_back(data == nullptr ? std::vector<std::size_t>{}
                                                       : data->extents);
      }
    }
  }
  result.target_names = source.target_names;
  result.target_symbols = source.target_symbols;
  result.target_pattern = lower_assignment_pattern(program, attributes.target_pattern);
  result.has_target_pattern = source.has_target_pattern;
  result.target_types.reserve(attributes.targets.size());
  result.target_numeric_types.reserve(attributes.targets.size());
  result.target_element_types.reserve(attributes.targets.size());
  result.target_element_numeric_types.reserve(attributes.targets.size());
  result.target_array_storage.reserve(attributes.targets.size());
  result.target_shapes.reserve(attributes.targets.size());
  result.target_previous_types.reserve(attributes.targets.size());
  result.target_previous_numeric_types.reserve(attributes.targets.size());
  result.target_previous_element_types.reserve(attributes.targets.size());
  result.target_previous_element_numeric_types.reserve(attributes.targets.size());
  result.target_previous_array_storage.reserve(attributes.targets.size());
  for (const auto& target : attributes.targets) {
    result.target_types.push_back(mir::value_type(program, target.type));
    result.target_numeric_types.push_back(mir::numeric_type(program, target.type));
    result.target_element_types.push_back(mir::element_type(program, target.type));
    result.target_element_numeric_types.push_back(mir::element_numeric_type(program, target.type));
    result.target_array_storage.push_back(mir::array_storage(program, target.type));
    const auto* target_shape = mir::shape(program, target.shape);
    result.target_shapes.push_back(target_shape == nullptr ? std::vector<std::size_t>{}
                                                           : target_shape->extents);
    result.target_previous_types.push_back(mir::value_type(program, target.previous_type));
    result.target_previous_numeric_types.push_back(
        mir::numeric_type(program, target.previous_type));
    result.target_previous_element_types.push_back(
        mir::element_type(program, target.previous_type));
    result.target_previous_element_numeric_types.push_back(
        mir::element_numeric_type(program, target.previous_type));
    result.target_previous_array_storage.push_back(
        mir::array_storage(program, target.previous_type));
  }
  result.indexed_mutation = attributes.indexed_mutation.contract;
  if (const auto* shape = mir::shape(program, attributes.indexed_mutation.input_shape);
      shape != nullptr) {
    result.mutation_input_shape = shape->extents;
  }
  if (const auto* shape = mir::shape(program, attributes.indexed_mutation.result_shape);
      shape != nullptr) {
    result.mutation_result_shape = shape->extents;
  }
  const auto& sparse = attributes.sparse_mutation;
  result.sparse_mutation.kind = sparse.kind;
  result.sparse_mutation.replacement = sparse.replacement;
  result.sparse_mutation.duplicate_policy = sparse.duplicate_policy;
  result.sparse_mutation.zero_policy = sparse.zero_policy;
  result.sparse_mutation.source_storage = sparse.source_storage;
  result.sparse_mutation.replacement_storage = sparse.replacement_storage;
  result.sparse_mutation.result_storage = sparse.result_storage;
  if (const auto* shape = mir::shape(program, sparse.input_shape); shape != nullptr) {
    result.sparse_mutation.input_shape = shape->extents;
  }
  if (const auto* shape = mir::shape(program, sparse.selection_shape); shape != nullptr) {
    result.sparse_mutation.selection_shape = shape->extents;
  }
  if (const auto* shape = mir::shape(program, sparse.replacement_shape); shape != nullptr) {
    result.sparse_mutation.replacement_shape = shape->extents;
  }
  if (const auto* shape = mir::shape(program, sparse.result_shape); shape != nullptr) {
    result.sparse_mutation.result_shape = shape->extents;
  }
  result.case_selectors.reserve(source.case_selectors.size());
  for (const auto& selector : source.case_selectors) {
    result.case_selectors.push_back(lower_lir_selector<LirSelector, LirExpression>(
        selector, ids, program, resolve_binding, call_sites));
  }
  result.default_case = source.default_case;
  result.has_exception_handler = source.has_exception_handler;
  result.exception_handler_line = source.exception_handler_line;
  result.body.reserve(source.body.size());
  for (const auto statement : source.body) {
    result.body.push_back(lower_lir_statement<LirStatement, LirExpression, LirSelector>(
        program, statement, ids, resolve_binding, call_sites));
  }
  result.alternative.reserve(source.alternative.size());
  for (const auto statement : source.alternative) {
    result.alternative.push_back(lower_lir_statement<LirStatement, LirExpression, LirSelector>(
        program, statement, ids, resolve_binding, call_sites));
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
  result->statements.reserve(source.roots.size());
  for (const auto statement : source.roots) {
    result->statements.push_back(lower_lir_statement<LirStatement, LirExpression, LirSelector>(
        source, statement, ids, resolve_binding, call_sites));
  }
  result->node_count = ids.count();
  return result;
}

}  // namespace mpf::detail
