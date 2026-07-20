#pragma once

#include <iomanip>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ir/semantics.hpp"

namespace mpf::detail {

template <typename Plan>
using TargetRepresentationDetails =
    decltype(std::declval<const Plan&>().concrete_type, std::declval<const Plan&>().widen_children,
             std::declval<const Plan&>().complex_children, std::declval<const Plan&>().flatten_base,
             std::declval<const Plan&>().call_outcome, void());

template <typename Plan, TargetRepresentationDetails<Plan>* = nullptr>
void dump_representation_details(std::ostream& output, const Plan& plan, int) {
  if (!plan.concrete_type.empty()) output << " concrete-type " << std::quoted(plan.concrete_type);
  if (!plan.widen_children.empty()) {
    output << " widen [";
    for (std::size_t index = 0; index < plan.widen_children.size(); ++index) {
      if (index != 0) output << ',';
      output << plan.widen_children[index];
    }
    output << ']';
  }
  if (!plan.complex_children.empty()) {
    output << " complex-widen [";
    for (std::size_t index = 0; index < plan.complex_children.size(); ++index) {
      if (index != 0) output << ',';
      output << plan.complex_children[index];
    }
    output << ']';
  }
  if (plan.flatten_base) output << " flatten-base 1";
  output << " call-outcome " << static_cast<int>(plan.call_outcome);
  if (!plan.input_shape.empty()) {
    output << " input-shape [";
    for (std::size_t index = 0; index < plan.input_shape.size(); ++index) {
      if (index != 0) output << ',';
      output << plan.input_shape[index];
    }
    output << "] result-shape [";
    for (std::size_t index = 0; index < plan.result_shape.size(); ++index) {
      if (index != 0) output << ',';
      output << plan.result_shape[index];
    }
    output << ']';
  }
}

template <typename Plan>
void dump_representation_details(std::ostream&, const Plan&, long) {}

template <typename Expression>
void dump_target_expression(std::ostream& output, const Expression& expression,
                            const std::size_t depth) {
  if (!expression.valid()) return;
  output << std::string(depth * 2U, ' ') << "expr %l" << expression.id.value() << " origin %h"
         << expression.origin.value() << " kind " << static_cast<int>(expression.kind) << " type "
         << static_cast<int>(expression.inferred_type) << " numeric "
         << static_cast<int>(expression.numeric_type.value_class) << '/'
         << static_cast<int>(expression.numeric_type.complexity) << " element "
         << static_cast<int>(expression.element_type) << " element-numeric "
         << static_cast<int>(expression.element_numeric_type.value_class) << '/'
         << static_cast<int>(expression.element_numeric_type.complexity) << " array-storage "
         << static_cast<int>(expression.array_storage) << " binding "
         << static_cast<int>(expression.binding) << " intrinsic "
         << static_cast<int>(expression.intrinsic) << " symbol @s" << expression.symbol_id.value()
         << " shape [";
  for (std::size_t index = 0; index < expression.shape.size(); ++index) {
    if (index != 0) output << ',';
    output << expression.shape[index];
  }
  output << "] value " << std::quoted(expression.value);
  output << " logical-evaluation " << static_cast<int>(expression.logical_evaluation);
  output << " plan " << static_cast<int>(expression.plan.form) << " precedence "
         << expression.plan.precedence << " token " << std::quoted(expression.plan.token)
         << " call " << static_cast<int>(expression.plan.call) << " evaluation "
         << static_cast<int>(expression.plan.evaluation) << " call-value "
         << static_cast<int>(expression.plan.call_value) << " index "
         << static_cast<int>(expression.plan.index) << " string-value "
         << expression.plan.string_value << " variable-access "
         << static_cast<int>(expression.plan.variable_access) << " index-base "
         << expression.plan.index_base << " negative-index " << expression.plan.allow_negative_index
         << " column-major " << expression.plan.column_major << " inclusive-slice "
         << expression.plan.inclusive_slice_stop;
  if (expression.plan.array_literal.form != decltype(expression.plan.array_literal.form)::none) {
    output << " array-literal " << static_cast<int>(expression.plan.array_literal.form)
           << " array-shape [";
    for (std::size_t axis = 0; axis < expression.plan.array_literal.shape.size(); ++axis) {
      if (axis != 0U) output << ',';
      output << expression.plan.array_literal.shape[axis];
    }
    output << ']';
  }
  if (expression.array_operation == semantic::ArrayOperation::matlab) {
    output << " matlab-array-operation 1";
  }
  if (expression.matrix_operation.valid()) {
    const auto dump_shape = [&](const std::vector<std::size_t>& shape) {
      output << '[';
      for (std::size_t axis = 0; axis < shape.size(); ++axis) {
        if (axis != 0U) output << ',';
        output << shape[axis];
      }
      output << ']';
    };
    output << " matrix-operation " << static_cast<int>(expression.matrix_operation.operation)
           << " solve " << static_cast<int>(expression.matrix_operation.solve) << " numeric-domain "
           << static_cast<int>(expression.matrix_operation.numeric_domain) << " condition-policy "
           << static_cast<int>(expression.matrix_operation.condition_policy)
           << " factorization-policy "
           << static_cast<int>(expression.matrix_operation.factorization_policy)
           << " structure-policy " << static_cast<int>(expression.matrix_operation.structure_policy)
           << " storage-policy " << static_cast<int>(expression.matrix_operation.storage_policy)
           << " exponent-policy " << static_cast<int>(expression.matrix_operation.exponent_policy)
           << " storage " << static_cast<int>(expression.matrix_operation.left_storage) << ','
           << static_cast<int>(expression.matrix_operation.right_storage) << "->"
           << static_cast<int>(expression.matrix_operation.result_storage) << ' ';
    dump_shape(expression.matrix_operation.left_shape);
    if (!expression.matrix_operation.right_shape.empty()) {
      output << ',';
      dump_shape(expression.matrix_operation.right_shape);
    }
    output << "->";
    dump_shape(expression.matrix_operation.result_shape);
  }
  if (expression.sparse_arithmetic.valid()) {
    const auto dump_shape = [&](const std::vector<std::size_t>& shape) {
      output << '[';
      for (std::size_t axis = 0; axis < shape.size(); ++axis) {
        if (axis != 0U) output << ',';
        output << shape[axis];
      }
      output << ']';
    };
    output << " sparse-arithmetic " << static_cast<int>(expression.sparse_arithmetic.operation)
           << " storage-policy " << static_cast<int>(expression.sparse_arithmetic.storage_policy)
           << " storage " << static_cast<int>(expression.sparse_arithmetic.left_storage) << ','
           << static_cast<int>(expression.sparse_arithmetic.right_storage) << "->"
           << static_cast<int>(expression.sparse_arithmetic.result_storage) << ' ';
    dump_shape(expression.sparse_arithmetic.left_shape);
    output << ',';
    dump_shape(expression.sparse_arithmetic.right_shape);
    output << "->";
    dump_shape(expression.sparse_arithmetic.result_shape);
  }
  if (expression.sparse_elementwise.valid()) {
    const auto dump_shape = [&](const std::vector<std::size_t>& shape) {
      output << '[';
      for (std::size_t axis = 0; axis < shape.size(); ++axis) {
        if (axis != 0U) output << ',';
        output << shape[axis];
      }
      output << ']';
    };
    output << " sparse-elementwise " << static_cast<int>(expression.sparse_elementwise.operation)
           << " storage-policy " << static_cast<int>(expression.sparse_elementwise.storage_policy)
           << " storage " << static_cast<int>(expression.sparse_elementwise.left_storage) << ','
           << static_cast<int>(expression.sparse_elementwise.right_storage) << "->"
           << static_cast<int>(expression.sparse_elementwise.result_storage) << ' ';
    dump_shape(expression.sparse_elementwise.left_shape);
    output << ',';
    dump_shape(expression.sparse_elementwise.right_shape);
    output << "->";
    dump_shape(expression.sparse_elementwise.result_shape);
  }
  if (expression.sparse_logical.valid()) {
    const auto dump_shape = [&](const std::vector<std::size_t>& shape) {
      output << '[';
      for (std::size_t axis = 0; axis < shape.size(); ++axis) {
        if (axis != 0U) output << ',';
        output << shape[axis];
      }
      output << ']';
    };
    output << " sparse-logical " << static_cast<int>(expression.sparse_logical.operation)
           << " storage-policy " << static_cast<int>(expression.sparse_logical.storage_policy)
           << " storage " << static_cast<int>(expression.sparse_logical.left_storage) << ','
           << static_cast<int>(expression.sparse_logical.right_storage) << "->"
           << static_cast<int>(expression.sparse_logical.result_storage) << ' ';
    dump_shape(expression.sparse_logical.left_shape);
    if (!expression.sparse_logical.right_shape.empty()) {
      output << ',';
      dump_shape(expression.sparse_logical.right_shape);
    }
    output << "->";
    dump_shape(expression.sparse_logical.result_shape);
  }
  if (expression.reduction.valid()) {
    const auto dump_shape = [&](const std::vector<std::size_t>& shape) {
      output << '[';
      for (std::size_t axis = 0; axis < shape.size(); ++axis) {
        if (axis != 0U) output << ',';
        output << shape[axis];
      }
      output << ']';
    };
    output << " reduction " << static_cast<int>(expression.reduction.operation) << " axis-policy "
           << static_cast<int>(expression.reduction.axis_policy) << " shape-source "
           << static_cast<int>(expression.reduction.shape_source) << " scalar "
           << expression.reduction.scalar_result << " storage-policy "
           << static_cast<int>(expression.reduction.storage_policy) << " storage "
           << static_cast<int>(expression.reduction.input_storage) << "->"
           << static_cast<int>(expression.reduction.result_storage) << ' ';
    dump_shape(expression.reduction.input_shape);
    output << " axes [";
    for (std::size_t axis = 0; axis < expression.reduction.axes.size(); ++axis) {
      if (axis != 0U) output << ',';
      output << expression.reduction.axes[axis];
    }
    output << "]->";
    dump_shape(expression.reduction.result_shape);
    output << " output ";
    dump_shape(expression.reduction.output_shape);
  }
  if (expression.sparse_construction.valid()) {
    output << " sparse-construction " << static_cast<int>(expression.sparse_construction.kind)
           << " shape [";
    for (std::size_t axis = 0; axis < expression.sparse_construction.result_shape.size(); ++axis) {
      if (axis != 0U) output << ',';
      output << expression.sparse_construction.result_shape[axis];
    }
    output << "] counts [";
    for (std::size_t index = 0;
         index < expression.sparse_construction.triplet_element_counts.size(); ++index) {
      if (index != 0U) output << ',';
      output << expression.sparse_construction.triplet_element_counts[index];
    }
    output << "] reserve " << expression.sparse_construction.reserve_hint << " value-domain "
           << static_cast<int>(expression.sparse_construction.value_domain) << " duplicate-policy "
           << static_cast<int>(expression.sparse_construction.duplicate_policy);
  }
  if (expression.sparse_index.valid()) {
    output << " sparse-index " << static_cast<int>(expression.sparse_index.kind) << " input [";
    for (std::size_t axis = 0; axis < expression.sparse_index.input_shape.size(); ++axis) {
      if (axis != 0U) output << ',';
      output << expression.sparse_index.input_shape[axis];
    }
    output << "] result [";
    for (std::size_t axis = 0; axis < expression.sparse_index.result_shape.size(); ++axis) {
      if (axis != 0U) output << ',';
      output << expression.sparse_index.result_shape[axis];
    }
    output << ']';
  }
  if (expression.sparse_reshape.valid()) {
    const auto dump_shape = [&](const std::vector<std::size_t>& shape) {
      output << '[';
      for (std::size_t axis = 0; axis < shape.size(); ++axis) {
        if (axis != 0U) output << ',';
        output << shape[axis];
      }
      output << ']';
    };
    output << " sparse-reshape " << static_cast<int>(expression.sparse_reshape.kind) << " form "
           << static_cast<int>(expression.sparse_reshape.dimension_form) << " inference "
           << static_cast<int>(expression.sparse_reshape.inference) << " axis "
           << expression.sparse_reshape.inferred_axis << " input ";
    dump_shape(expression.sparse_reshape.input_shape);
    output << " requested ";
    dump_shape(expression.sparse_reshape.requested_shape);
    output << " result ";
    dump_shape(expression.sparse_reshape.result_shape);
  }
  if (!expression.plan.call_arguments.empty()) {
    output << " call-arguments [";
    for (std::size_t index = 0; index < expression.plan.call_arguments.size(); ++index) {
      if (index != 0) output << ',';
      output << static_cast<int>(expression.plan.call_arguments[index].form) << ':'
             << static_cast<int>(expression.plan.call_arguments[index].writeback);
    }
    output << ']';
  }
  if (!expression.plan.runtime_shape_arguments.empty()) {
    output << " runtime-shape-arguments [";
    for (std::size_t argument = 0; argument < expression.plan.runtime_shape_arguments.size();
         ++argument) {
      if (argument != 0U) output << ',';
      output << '[';
      const auto& shape = expression.plan.runtime_shape_arguments[argument];
      for (std::size_t axis = 0; axis < shape.size(); ++axis) {
        if (axis != 0U) output << ',';
        output << shape[axis];
      }
      output << ']';
    }
    output << ']';
  }
  if (!expression.plan.runtime_integer_arguments.empty()) {
    output << " runtime-integer-arguments [";
    for (std::size_t argument = 0U; argument < expression.plan.runtime_integer_arguments.size();
         ++argument) {
      if (argument != 0U) output << ',';
      output << expression.plan.runtime_integer_arguments[argument];
    }
    output << ']';
  }
  if (!expression.plan.comparisons.empty()) {
    output << " comparisons [";
    for (std::size_t index = 0; index < expression.plan.comparisons.size(); ++index) {
      if (index != 0) output << ',';
      output << static_cast<int>(expression.plan.comparisons[index].form) << ':'
             << std::quoted(expression.plan.comparisons[index].token);
    }
    output << ']';
  }
  if (!expression.plan.index_selectors.empty()) {
    output << " selectors [";
    for (std::size_t index = 0; index < expression.plan.index_selectors.size(); ++index) {
      if (index != 0) output << ',';
      output << static_cast<int>(expression.plan.index_selectors[index]);
    }
    output << ']';
  }
  if (!expression.plan.index_extents.empty()) {
    output << " extents [";
    for (std::size_t index = 0; index < expression.plan.index_extents.size(); ++index) {
      if (index != 0) output << ',';
      output << static_cast<int>(expression.plan.index_extents[index]);
    }
    output << ']';
  }
  if (expression.plan.broadcast.valid) {
    const auto dump_shape = [&](const std::vector<std::size_t>& shape) {
      output << '[';
      for (std::size_t axis = 0; axis < shape.size(); ++axis) {
        if (axis != 0U) output << ',';
        output << shape[axis];
      }
      output << ']';
    };
    output << " broadcast ";
    output << (expression.plan.broadcast.shape_source ==
                       semantic::BroadcastShapeSource::runtime_operands
                   ? "runtime "
                   : "static ");
    dump_shape(expression.plan.broadcast.left_shape);
    output << ',';
    dump_shape(expression.plan.broadcast.right_shape);
    output << "->";
    dump_shape(expression.plan.broadcast.result_shape);
    output << " axes [";
    for (std::size_t axis = 0; axis < expression.plan.broadcast.axes.size(); ++axis) {
      if (axis != 0U) output << ',';
      output << static_cast<int>(expression.plan.broadcast.axes[axis]);
    }
    output << ']';
  }
  dump_representation_details(output, expression.plan, 0);
  if (!expression.argument_transfers.empty()) {
    output << " transfers [";
    for (std::size_t index = 0; index < expression.argument_transfers.size(); ++index) {
      if (index != 0) output << ',';
      output << static_cast<int>(expression.argument_transfers[index]);
    }
    output << ']';
  }
  output << '\n';
  for (const auto& child : expression.children) {
    dump_target_expression(output, child, depth + 1U);
  }
}

template <typename Statement>
void dump_target_statements(std::ostream& output, const std::vector<Statement>& statements,
                            const std::size_t depth) {
  for (const auto& statement : statements) {
    output << std::string(depth * 2U, ' ') << "stmt %l" << statement.id.value() << " origin %h"
           << statement.origin.value() << " kind " << static_cast<int>(statement.kind) << " line "
           << statement.line << " name " << std::quoted(statement.name) << " symbol @s"
           << statement.symbol_id.value() << " declared "
           << static_cast<int>(statement.declared_type) << " numeric "
           << static_cast<int>(statement.declared_numeric_type.value_class) << '/'
           << static_cast<int>(statement.declared_numeric_type.complexity) << " element "
           << static_cast<int>(statement.element_type) << " element-numeric "
           << static_cast<int>(statement.element_numeric_type.value_class) << '/'
           << static_cast<int>(statement.element_numeric_type.complexity) << " array-storage "
           << static_cast<int>(statement.array_storage) << " plan "
           << static_cast<int>(statement.plan.form) << " condition "
           << static_cast<int>(statement.plan.condition) << " target-access "
           << static_cast<int>(statement.plan.target_access) << " alternative "
           << statement.plan.has_alternative << " range-step " << statement.plan.range_has_step
           << " retain-loop " << statement.plan.retain_loop_value << " inclusive-stop "
           << statement.plan.inclusive_stop << " resizable-section "
           << statement.plan.resizable_section << " character-selector "
           << statement.plan.character_selector << " mutation "
           << static_cast<int>(statement.plan.indexed_mutation.kind) << " mutation-shape-source "
           << static_cast<int>(statement.plan.indexed_mutation.shape_source) << " mutation-linear "
           << statement.plan.indexed_mutation.linear << " mutation-axis "
           << (statement.plan.indexed_mutation.kind == semantic::IndexedMutationKind::erase
                   ? statement.plan.indexed_mutation.axis
                   : 0U)
           << " sparse-mutation " << static_cast<int>(statement.plan.sparse_mutation.kind)
           << " sparse-replacement " << static_cast<int>(statement.plan.sparse_mutation.replacement)
           << " sparse-duplicate "
           << static_cast<int>(statement.plan.sparse_mutation.duplicate_policy) << " sparse-zero "
           << static_cast<int>(statement.plan.sparse_mutation.zero_policy) << " targets "
           << statement.plan.targets.size() << " assignment-leaves "
           << statement.plan.assignment_leaves.size() << " selectors "
           << statement.plan.selectors.size() << " returns " << statement.plan.return_names.size()
           << '\n';
    dump_target_expression(output, statement.expression, depth + 1U);
    dump_target_expression(output, statement.secondary_expression, depth + 1U);
    dump_target_expression(output, statement.tertiary_expression, depth + 1U);
    dump_target_expression(output, statement.target_expression, depth + 1U);
    for (const auto& expression : statement.parameter_defaults) {
      dump_target_expression(output, expression, depth + 1U);
    }
    for (const auto& selector : statement.case_selectors) {
      dump_target_expression(output, selector.lower, depth + 1U);
      dump_target_expression(output, selector.upper, depth + 1U);
    }
    dump_target_statements(output, statement.body, depth + 1U);
    dump_target_statements(output, statement.alternative, depth + 1U);
  }
}

template <typename Program>
void dump_target_lir_body(std::ostream& output, const Program& program,
                          const std::string_view target) {
  output << target << "-semantic-lir-v40 revision " << program.revision << " nodes "
         << program.node_count << " runtime 0x" << std::hex << program.runtime.bits << std::dec
         << '\n';
  output << "dependencies";
  for (const auto dependency : program.dependencies) output << ' ' << dependency;
  output << '\n';
  output << "temporaries\n";
  for (std::size_t node = 1; node + 1U < program.temporaries.offsets.size(); ++node) {
    for (std::size_t index = program.temporaries.offsets[node];
         index < program.temporaries.offsets[node + 1U]; ++index) {
      const auto& slot = program.temporaries.slots[index];
      output << "  %l" << node << " role " << static_cast<int>(slot.role) << " ordinal "
             << slot.ordinal << " name " << std::quoted(slot.name) << '\n';
    }
  }
  output << "source-segments\n";
  for (std::size_t node = 1; node < program.source_segments.nodes.size(); ++node) {
    const auto& segment = program.source_segments.nodes[node];
    if (!segment.valid()) continue;
    output << "  %l" << segment.node.value() << " origin %h" << segment.origin.value() << " at "
           << segment.source.line << ':' << segment.source.column << '\n';
  }
  dump_target_statements(output, program.statements, 0);
}

}  // namespace mpf::detail
