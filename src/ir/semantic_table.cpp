#include "semantic_table.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "compiler/numeric_contract.hpp"

namespace mpf::detail::hir {
namespace {

template <typename Facts>
bool valid_offset(const std::uint32_t offset, const std::vector<Facts>& facts) noexcept {
  return static_cast<std::size_t>(offset) < facts.size();
}

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               const std::string_view stage, std::string message) {
  diagnostics.push_back(
      {DiagnosticSeverity::error, "MPF0005",
       "invalid semantic table at '" + std::string(stage) + "': " + std::move(message), location});
}

std::string numeric_contract_summary(const ValueType type, const NumericType numeric_type) {
  return std::to_string(static_cast<unsigned>(type)) + ':' +
         std::to_string(static_cast<unsigned>(numeric_type.value_class)) + '/' +
         std::to_string(static_cast<unsigned>(numeric_type.complexity));
}

NumericType expression_numeric_type(const ExpressionFacts& facts) noexcept {
  return facts.inferred_type == ValueType::list ? facts.element_numeric_type : facts.numeric_type;
}

semantic::MatrixOperation matrix_operation_for_operator(const BinaryOperator operation) noexcept {
  switch (operation) {
    case BinaryOperator::multiply: return semantic::MatrixOperation::multiply;
    case BinaryOperator::left_divide: return semantic::MatrixOperation::left_divide;
    case BinaryOperator::divide: return semantic::MatrixOperation::right_divide;
    case BinaryOperator::power: return semantic::MatrixOperation::integer_power;
    default: return semantic::MatrixOperation::none;
  }
}

semantic::MatrixOperation expected_matrix_operation(const Expression& expression,
                                                    const ExpressionFacts& facts,
                                                    const SemanticTable& table) noexcept {
  if (facts.array_operation != semantic::ArrayOperation::matlab) {
    return semantic::MatrixOperation::none;
  }
  if (expression.children.size() != 2U) return semantic::MatrixOperation::none;
  const auto* left = table.expression(expression.children[0].id);
  const auto* right = table.expression(expression.children[1].id);
  if (left == nullptr || right == nullptr) return semantic::MatrixOperation::none;
  const bool left_array = left->inferred_type == ValueType::list;
  const bool right_array = right->inferred_type == ValueType::list;
  if (expression.operation == BinaryOperator::multiply && left_array && right_array) {
    return semantic::MatrixOperation::multiply;
  }
  if (expression.operation == BinaryOperator::left_divide && left_array && right_array) {
    return semantic::MatrixOperation::left_divide;
  }
  if (expression.operation == BinaryOperator::divide && left_array && right_array) {
    return semantic::MatrixOperation::right_divide;
  }
  if (expression.operation == BinaryOperator::power && left_array && !right_array) {
    return semantic::MatrixOperation::integer_power;
  }
  return semantic::MatrixOperation::none;
}

std::optional<semantic::MatrixNumericDomain> expected_matrix_numeric_domain(
    const Expression& expression, const SemanticTable& table) noexcept {
  if (expression.children.size() != 2U) return std::nullopt;
  const auto* left = table.expression(expression.children[0].id);
  const auto* right = table.expression(expression.children[1].id);
  if (left == nullptr || right == nullptr) return std::nullopt;
  const auto left_numeric = expression_numeric_type(*left);
  const auto right_numeric = expression_numeric_type(*right);
  if (!left_numeric.present() || left_numeric.complexity == NumericComplexity::unknown) {
    return std::nullopt;
  }
  if (expression.operation == BinaryOperator::power && left->inferred_type == ValueType::list &&
      right->inferred_type != ValueType::list) {
    if (!right_numeric.present() || right_numeric.complexity != NumericComplexity::real) {
      return std::nullopt;
    }
    return left_numeric.complexity == NumericComplexity::complex
               ? semantic::MatrixNumericDomain::complex
               : semantic::MatrixNumericDomain::real;
  }
  if (!right_numeric.present() || right_numeric.complexity == NumericComplexity::unknown) {
    return std::nullopt;
  }
  return left_numeric.complexity == NumericComplexity::complex ||
                 right_numeric.complexity == NumericComplexity::complex
             ? semantic::MatrixNumericDomain::complex
             : semantic::MatrixNumericDomain::real;
}

bool static_rank_two(const std::vector<std::size_t>& shape) noexcept {
  return shape.size() == 2U && std::find(shape.begin(), shape.end(), dynamic_extent) == shape.end();
}

std::optional<semantic::IndexSelectorKind> expected_index_selector(
    const Expression& selector, const SemanticTable& table) noexcept {
  if (selector.kind == ExpressionKind::slice) return semantic::IndexSelectorKind::slice;
  const auto* facts = table.expression(selector.id);
  if (facts == nullptr || facts->inferred_type == ValueType::unknown) return std::nullopt;
  if (facts->inferred_type != ValueType::list) return semantic::IndexSelectorKind::scalar;
  if (std::find(facts->shape.begin(), facts->shape.end(), 0U) != facts->shape.end()) {
    return semantic::IndexSelectorKind::empty;
  }
  if (facts->element_type == ValueType::boolean) return semantic::IndexSelectorKind::logical;
  if (facts->element_type == ValueType::integer || facts->element_type == ValueType::real) {
    return semantic::IndexSelectorKind::numeric;
  }
  return std::nullopt;
}

void collect_index_extent(const Expression& expression, const SemanticTable& table,
                          std::optional<semantic::IndexExtentSource>& source, bool& valid) {
  const auto* facts = table.expression(expression.id);
  if (expression.kind == ExpressionKind::end_index) {
    if (facts == nullptr || !semantic::requires_runtime_extent(facts->index_extent) ||
        (source.has_value() && *source != facts->index_extent)) {
      valid = false;
      return;
    }
    source = facts->index_extent;
    return;
  }
  for (const auto& child : expression.children) {
    collect_index_extent(child, table, source, valid);
  }
}

bool valid_matrix_shapes(const MatrixOperationPlan& plan) noexcept {
  if (!static_rank_two(plan.left_shape) || !static_rank_two(plan.result_shape) ||
      plan.numeric_domain == semantic::MatrixNumericDomain::none ||
      plan.condition_policy != semantic::matrix_condition_policy(plan.solve) ||
      plan.storage_policy !=
          semantic::matrix_storage_policy(plan.operation, plan.left_storage, plan.right_storage) ||
      plan.factorization_policy !=
          semantic::matrix_factorization_policy(plan.solve, plan.storage_policy) ||
      plan.structure_policy !=
          semantic::matrix_structure_policy(plan.solve, plan.numeric_domain, plan.storage_policy) ||
      !array_storage_known(plan.left_storage) ||
      (plan.operation != semantic::MatrixOperation::integer_power &&
       !array_storage_known(plan.right_storage)) ||
      !array_storage_known(plan.result_storage)) {
    return false;
  }
  switch (plan.operation) {
    case semantic::MatrixOperation::none: return false;
    case semantic::MatrixOperation::multiply:
      return plan.solve == semantic::MatrixSolveKind::none && static_rank_two(plan.right_shape) &&
             plan.storage_policy == semantic::MatrixStoragePolicy::dense &&
             plan.left_storage == ArrayStorageFormat::dense &&
             plan.right_storage == ArrayStorageFormat::dense &&
             plan.result_storage == ArrayStorageFormat::dense &&
             plan.left_shape[1] == plan.right_shape[0] &&
             plan.result_shape[0] == plan.left_shape[0] &&
             plan.result_shape[1] == plan.right_shape[1];
    case semantic::MatrixOperation::left_divide:
      return static_rank_two(plan.right_shape) && plan.left_shape[0] == plan.right_shape[0] &&
             plan.result_storage == plan.right_storage &&
             (plan.storage_policy != semantic::MatrixStoragePolicy::sparse_csc_coefficient ||
              (plan.solve == semantic::MatrixSolveKind::square &&
               plan.numeric_domain == semantic::MatrixNumericDomain::real)) &&
             plan.solve == semantic::matrix_solve_kind(plan.left_shape[0], plan.left_shape[1]) &&
             plan.result_shape[0] == plan.left_shape[1] &&
             plan.result_shape[1] == plan.right_shape[1];
    case semantic::MatrixOperation::right_divide:
      return static_rank_two(plan.right_shape) && plan.left_shape[1] == plan.right_shape[1] &&
             plan.result_storage == plan.left_storage &&
             (plan.storage_policy != semantic::MatrixStoragePolicy::sparse_csc_coefficient ||
              (plan.solve == semantic::MatrixSolveKind::square &&
               plan.numeric_domain == semantic::MatrixNumericDomain::real)) &&
             plan.solve == semantic::matrix_solve_kind(plan.right_shape[1], plan.right_shape[0]) &&
             plan.result_shape[0] == plan.left_shape[0] &&
             plan.result_shape[1] == plan.right_shape[0];
    case semantic::MatrixOperation::integer_power:
      return plan.solve == semantic::MatrixSolveKind::none && plan.right_shape.empty() &&
             plan.storage_policy == semantic::MatrixStoragePolicy::dense &&
             plan.left_storage == ArrayStorageFormat::dense &&
             plan.right_storage == ArrayStorageFormat::none &&
             plan.result_storage == ArrayStorageFormat::dense &&
             plan.left_shape[0] == plan.left_shape[1] && plan.result_shape == plan.left_shape;
  }
  return false;
}

bool valid_array_storage_contract(const ValueType type, const ArrayStorageFormat storage) noexcept {
  if (type == ValueType::list) {
    return storage == ArrayStorageFormat::unknown || array_storage_known(storage);
  }
  if (type == ValueType::unknown) {
    return storage == ArrayStorageFormat::none || storage == ArrayStorageFormat::unknown;
  }
  return storage == ArrayStorageFormat::none;
}

bool logical_composition(const BinaryOperator operation) noexcept {
  return operation == BinaryOperator::logical_and || operation == BinaryOperator::logical_or ||
         operation == BinaryOperator::elementwise_logical_and ||
         operation == BinaryOperator::elementwise_logical_or;
}

semantic::LogicalEvaluation expected_logical_evaluation(const Expression& expression,
                                                        const SourceLanguage source_language,
                                                        const bool condition_context) noexcept {
  if (expression.kind == ExpressionKind::unary && source_language == SourceLanguage::matlab &&
      expression.unary_operation == UnaryOperator::logical_not) {
    return semantic::LogicalEvaluation::eager_elementwise;
  }
  if (expression.kind != ExpressionKind::binary) return semantic::LogicalEvaluation::none;
  if (expression.operation == BinaryOperator::logical_and ||
      expression.operation == BinaryOperator::logical_or) {
    return source_language == SourceLanguage::python
               ? semantic::LogicalEvaluation::short_circuit_operand
               : semantic::LogicalEvaluation::short_circuit_boolean;
  }
  if (source_language == SourceLanguage::matlab &&
      (expression.operation == BinaryOperator::elementwise_logical_and ||
       expression.operation == BinaryOperator::elementwise_logical_or)) {
    return condition_context ? semantic::LogicalEvaluation::short_circuit_boolean
                             : semantic::LogicalEvaluation::eager_elementwise;
  }
  return semantic::LogicalEvaluation::none;
}

semantic::ReductionOperation expected_reduction_operation(const Expression& expression,
                                                          const SemanticTable& table,
                                                          const SourceLanguage language) noexcept {
  if (language != SourceLanguage::matlab || expression.kind != ExpressionKind::call ||
      expression.children.empty()) {
    return semantic::ReductionOperation::none;
  }
  const auto* callee = table.expression(expression.children.front().id);
  if (callee == nullptr || callee->binding != BindingKind::builtin) {
    return semantic::ReductionOperation::none;
  }
  if (callee->intrinsic == IntrinsicId::logical_all) {
    return semantic::ReductionOperation::logical_all;
  }
  if (callee->intrinsic == IntrinsicId::logical_any) {
    return semantic::ReductionOperation::logical_any;
  }
  return semantic::ReductionOperation::none;
}

void verify_expression(const Expression& expression, const SemanticTable& table,
                       const SourceLanguage source_language, std::vector<bool>& seen,
                       const std::string_view stage, std::vector<Diagnostic>& diagnostics,
                       const bool condition_context = false) {
  if (!expression.valid()) return;
  const auto* facts = table.expression(expression.id);
  if (facts == nullptr || facts->origin != expression.id) {
    add_error(diagnostics, expression.location, stage,
              "expression fact is missing or has the wrong origin");
    return;
  }
  seen[expression.id.value()] = true;
  const bool analyzed = stage != "ast-to-hir" && stage != "frontend-seed" && stage != "conformance";
  if (analyzed && !expression_numeric_contract_matches(*facts)) {
    add_error(diagnostics, expression.location, stage,
              "expression numeric metadata is inconsistent (value " +
                  numeric_contract_summary(facts->inferred_type, facts->numeric_type) +
                  ", element " +
                  numeric_contract_summary(facts->element_type, facts->element_numeric_type) +
                  ", tuple arity " + std::to_string(facts->tuple_types.size()) + ')');
  }
  if (analyzed && !valid_array_storage_contract(facts->inferred_type, facts->array_storage)) {
    add_error(diagnostics, expression.location, stage,
              "expression array-storage format disagrees with its value type");
  }
  if (analyzed && facts->logical_evaluation !=
                      expected_logical_evaluation(expression, source_language, condition_context)) {
    add_error(diagnostics, expression.location, stage,
              "logical evaluation policy disagrees with the source operator context");
  }
  if (analyzed) {
    const auto expected_reduction =
        expected_reduction_operation(expression, table, source_language);
    const auto& reduction = facts->reduction;
    if (reduction.operation != expected_reduction) {
      add_error(diagnostics, expression.location, stage,
                "logical reduction identity disagrees with the source intrinsic");
    } else if (reduction.valid()) {
      const bool valid_contract = semantic::valid_logical_reduction_contract(
          reduction.operation, reduction.axis_policy, reduction.shape_source, reduction.input_shape,
          reduction.result_shape, reduction.output_shape, reduction.axes, reduction.scalar_result);
      const bool valid_type =
          reduction.scalar_result
              ? facts->inferred_type == ValueType::boolean && facts->shape.empty()
              : facts->inferred_type == ValueType::list &&
                    facts->element_type == ValueType::boolean;
      if (!valid_contract || !valid_type || facts->shape != reduction.output_shape) {
        add_error(diagnostics, expression.location, stage,
                  "logical reduction has an invalid type, axis, or shape contract");
      } else if (reduction.shape_source == semantic::ReductionShapeSource::static_extents &&
                 expression.children.size() >= 2U) {
        const auto* operand = table.expression(expression.children[1].id);
        auto operand_shape = operand == nullptr ? std::vector<std::size_t>{} : operand->shape;
        if (operand_shape.size() == 1U) operand_shape.insert(operand_shape.begin(), 1U);
        if (operand != nullptr && operand->inferred_type == ValueType::list &&
            operand_shape != reduction.input_shape) {
          add_error(diagnostics, expression.location, stage,
                    "logical reduction input shape disagrees with its operand");
        }
      }
    } else if (reduction.axis_policy != semantic::ReductionAxisPolicy::none ||
               reduction.shape_source != semantic::ReductionShapeSource::static_extents ||
               !reduction.input_shape.empty() || !reduction.result_shape.empty() ||
               !reduction.output_shape.empty() || !reduction.axes.empty() ||
               reduction.scalar_result) {
      add_error(diagnostics, expression.location, stage,
                "inactive logical reduction retains semantic state");
    }
  }
  if (analyzed && source_language == SourceLanguage::matlab &&
      expression.kind == ExpressionKind::list && expression.children.empty() &&
      (facts->inferred_type != ValueType::list || facts->element_type != ValueType::real ||
       facts->shape != std::vector<std::size_t>{0U, 0U} || !facts->column_major)) {
    add_error(diagnostics, expression.location, stage,
              "Matlab empty literal must be a column-major 0-by-0 double array");
  }
  if (facts->requested_outputs == 0) {
    add_error(diagnostics, expression.location, stage, "expression requests zero outputs");
  }
  if (analyzed && (expression.kind == ExpressionKind::end_index) !=
                      semantic::requires_runtime_extent(facts->index_extent)) {
    add_error(diagnostics, expression.location, stage,
              "runtime index-extent fact has invalid expression ownership");
  }
  if (facts->array_operation == semantic::ArrayOperation::matlab &&
      expression.kind != ExpressionKind::binary) {
    add_error(diagnostics, expression.location, stage,
              "Matlab array-operation facts require a binary expression");
  }
  if (facts->broadcast.valid) {
    const auto& broadcast = facts->broadcast;
    const auto rank = broadcast.axes.size();
    const bool runtime = broadcast.shape_source == semantic::BroadcastShapeSource::runtime_operands;
    const bool valid_unknown_rank = runtime && rank == 0U && broadcast.left_shape.empty() &&
                                    broadcast.right_shape.empty() && broadcast.result_shape.empty();
    if (facts->array_operation != semantic::ArrayOperation::matlab ||
        expression.kind != ExpressionKind::binary || (!valid_unknown_rank && rank == 0U) ||
        broadcast.left_shape.size() != rank || broadcast.right_shape.size() != rank ||
        broadcast.result_shape.size() != rank || facts->shape != broadcast.result_shape) {
      add_error(diagnostics, expression.location, stage,
                "expression broadcast plan has an invalid kind, arity, or result shape");
    } else {
      bool has_runtime_axis = false;
      bool valid_axes = true;
      for (std::size_t axis = 0; axis < rank; ++axis) {
        const auto left = broadcast.left_shape[axis];
        const auto right = broadcast.right_shape[axis];
        const auto result = broadcast.result_shape[axis];
        const auto mode = broadcast.axes[axis];
        const auto known = left == dynamic_extent ? right : left;
        const auto runtime_result = known == dynamic_extent || known == 1U ? dynamic_extent : known;
        valid_axes =
            valid_axes &&
            ((mode == semantic::BroadcastAxis::match && left == right && result == left) ||
             (mode == semantic::BroadcastAxis::expand_left && left == 1U && result == right) ||
             (mode == semantic::BroadcastAxis::expand_right && right == 1U && result == left) ||
             (runtime && mode == semantic::BroadcastAxis::runtime &&
              (left == dynamic_extent || right == dynamic_extent) && result == runtime_result));
        has_runtime_axis = has_runtime_axis || mode == semantic::BroadcastAxis::runtime;
      }
      if (!valid_axes || (rank != 0U && runtime != has_runtime_axis)) {
        add_error(diagnostics, expression.location, stage,
                  "expression broadcast axes disagree with their shape-source contract");
      }
    }
  } else if (facts->broadcast.shape_source != semantic::BroadcastShapeSource::static_extents) {
    add_error(diagnostics, expression.location, stage,
              "inactive expression broadcast plan retains a runtime shape source");
  }
  const auto& matrix = facts->matrix_operation;
  const auto expected_matrix = expected_matrix_operation(expression, *facts, table);
  if (matrix.operation != expected_matrix) {
    add_error(diagnostics, expression.location, stage,
              "matrix-operation identity disagrees with the typed operands");
  } else if (matrix.valid()) {
    const auto expected_domain = expected_matrix_numeric_domain(expression, table);
    const auto* left =
        expression.children.size() == 2U ? table.expression(expression.children[0].id) : nullptr;
    const auto* right =
        expression.children.size() == 2U ? table.expression(expression.children[1].id) : nullptr;
    if (facts->array_operation != semantic::ArrayOperation::matlab ||
        expression.kind != ExpressionKind::binary || facts->broadcast.valid ||
        matrix.operation != matrix_operation_for_operator(expression.operation) ||
        !expected_domain.has_value() || matrix.numeric_domain != *expected_domain ||
        left == nullptr || right == nullptr || matrix.left_storage != left->array_storage ||
        matrix.right_storage != right->array_storage ||
        facts->array_storage != matrix.result_storage || facts->shape != matrix.result_shape ||
        !valid_matrix_shapes(matrix)) {
      add_error(
          diagnostics, expression.location, stage,
          "matrix-operation plan has an invalid operator, shape, storage, or result "
          "contract (left-storage=" +
              std::to_string(static_cast<unsigned>(matrix.left_storage)) +
              ", right-storage=" + std::to_string(static_cast<unsigned>(matrix.right_storage)) +
              ", result-storage=" + std::to_string(static_cast<unsigned>(matrix.result_storage)) +
              ", expression-storage=" +
              std::to_string(static_cast<unsigned>(facts->array_storage)) + ")");
    }
  } else if (matrix.solve != semantic::MatrixSolveKind::none ||
             matrix.numeric_domain != semantic::MatrixNumericDomain::none ||
             matrix.condition_policy != semantic::MatrixConditionPolicy::none ||
             matrix.factorization_policy != semantic::MatrixFactorizationPolicy::none ||
             matrix.structure_policy != semantic::MatrixStructurePolicy::none ||
             matrix.storage_policy != semantic::MatrixStoragePolicy::none ||
             matrix.left_storage != ArrayStorageFormat::none ||
             matrix.right_storage != ArrayStorageFormat::none ||
             matrix.result_storage != ArrayStorageFormat::none || !matrix.left_shape.empty() ||
             !matrix.right_shape.empty() || !matrix.result_shape.empty()) {
    add_error(diagnostics, expression.location, stage,
              "inactive matrix-operation plan retains shape facts");
  }
  if (expression.kind == ExpressionKind::index) {
    if (facts->index_selectors.size() + 1U != expression.children.size() ||
        facts->index_extents.size() != facts->index_selectors.size()) {
      add_error(diagnostics, expression.location, stage,
                "index selector or extent facts disagree with the expression arity");
    } else {
      for (std::size_t index = 0; index < facts->index_selectors.size(); ++index) {
        const auto expected = expected_index_selector(expression.children[index + 1U], table);
        if (expected.has_value() && *expected != facts->index_selectors[index]) {
          add_error(diagnostics, expression.location, stage,
                    "index selector kind disagrees with the source expression");
          break;
        }
        if (analyzed) {
          std::optional<semantic::IndexExtentSource> extent;
          bool valid_extent = true;
          collect_index_extent(expression.children[index + 1U], table, extent, valid_extent);
          const auto expected_extent = extent.value_or(semantic::IndexExtentSource::none);
          if (!valid_extent || facts->index_extents[index] != expected_extent) {
            add_error(diagnostics, expression.location, stage,
                      "index runtime-extent plan disagrees with the selector expression");
            break;
          }
        }
      }
    }
  } else if (!facts->index_selectors.empty() || !facts->index_extents.empty()) {
    add_error(diagnostics, expression.location, stage,
              "non-index expression retains selector or extent facts");
  }
  if (!valid_storage_region(facts->storage_region) ||
      (facts->storage_region.kind != StorageRegionKind::unknown &&
       expression.kind != ExpressionKind::identifier && expression.kind != ExpressionKind::index &&
       expression.kind != ExpressionKind::slice)) {
    add_error(diagnostics, expression.location, stage,
              "expression storage-region fact is invalid for its expression kind");
  }
  if (facts->tuple_types.size() != facts->tuple_numeric_types.size() ||
      facts->tuple_types.size() != facts->tuple_element_types.size() ||
      facts->tuple_types.size() != facts->tuple_element_numeric_types.size() ||
      facts->tuple_types.size() != facts->tuple_array_storage.size() ||
      facts->tuple_types.size() != facts->tuple_shapes.size()) {
    add_error(diagnostics, expression.location, stage,
              "tuple type, numeric-class, element-type, storage, and shape arities disagree");
  }
  if (expression.kind == ExpressionKind::call && !facts->argument_names.empty() &&
      facts->argument_names.size() + 1U != expression.children.size()) {
    add_error(diagnostics, expression.location, stage,
              "normalized call argument-name arity disagrees with HIR");
  }
  const bool child_condition = condition_context && expression.kind == ExpressionKind::binary &&
                               logical_composition(expression.operation);
  for (const auto& child : expression.children) {
    verify_expression(child, table, source_language, seen, stage, diagnostics, child_condition);
  }
}

bool compatible_arity(const std::size_t size, const std::size_t expected) noexcept {
  return size == 0 || size == expected;
}

void verify_statements(const std::vector<Statement>& statements, const SemanticTable& table,
                       const SourceLanguage source_language, std::vector<bool>& seen,
                       const std::string_view stage, std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    const auto* facts = table.statement(statement.id);
    if (facts == nullptr || facts->origin != statement.id) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "statement fact is missing or has the wrong origin");
      continue;
    }
    seen[statement.id.value()] = true;
    const bool analyzed =
        stage != "ast-to-hir" && stage != "frontend-seed" && stage != "conformance";
    if (analyzed && !statement_numeric_contract_matches(*facts)) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "statement numeric metadata is inconsistent (declared " +
                    numeric_contract_summary(facts->declared_type, facts->declared_numeric_type) +
                    ", element " +
                    numeric_contract_summary(facts->element_type, facts->element_numeric_type) +
                    ')');
    }
    if (analyzed &&
        (!valid_array_storage_contract(facts->declared_type, facts->array_storage) ||
         !valid_array_storage_contract(facts->previous_type, facts->previous_array_storage))) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "statement array-storage format disagrees with its value type");
    }
    const auto parameters = statement.parameters.size();
    if (facts->exported && statement.kind != StatementKind::function) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "only a function statement may carry the exported semantic fact");
    }
    if (!compatible_arity(facts->parameter_intents.size(), parameters) ||
        !compatible_arity(facts->parameter_optional.size(), parameters) ||
        !compatible_arity(facts->parameter_types.size(), parameters) ||
        !compatible_arity(facts->parameter_numeric_types.size(), parameters) ||
        !compatible_arity(facts->parameter_element_types.size(), parameters) ||
        !compatible_arity(facts->parameter_element_numeric_types.size(), parameters) ||
        !compatible_arity(facts->parameter_array_storage.size(), parameters) ||
        !compatible_arity(facts->parameter_shapes.size(), parameters)) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "function parameter semantic arity disagrees with HIR");
    }
    const auto returns = statement.return_names.size();
    if (returns != 0 && (!compatible_arity(facts->return_types.size(), returns) ||
                         !compatible_arity(facts->return_numeric_types.size(), returns) ||
                         !compatible_arity(facts->return_element_types.size(), returns) ||
                         !compatible_arity(facts->return_element_numeric_types.size(), returns) ||
                         !compatible_arity(facts->return_array_storage.size(), returns) ||
                         !compatible_arity(facts->return_shapes.size(), returns))) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "function result semantic arity disagrees with HIR");
    }
    const auto targets = statement.target_names.size();
    if (statement.has_target_pattern && !facts->target_pattern.valid()) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "analyzed assignment pattern is missing from the semantic table");
    }
    if (!compatible_arity(facts->target_types.size(), targets) ||
        !compatible_arity(facts->target_numeric_types.size(), targets) ||
        !compatible_arity(facts->target_element_types.size(), targets) ||
        !compatible_arity(facts->target_element_numeric_types.size(), targets) ||
        !compatible_arity(facts->target_array_storage.size(), targets) ||
        !compatible_arity(facts->target_shapes.size(), targets) ||
        !compatible_arity(facts->target_previous_types.size(), targets) ||
        !compatible_arity(facts->target_previous_numeric_types.size(), targets) ||
        !compatible_arity(facts->target_previous_element_types.size(), targets) ||
        !compatible_arity(facts->target_previous_element_numeric_types.size(), targets) ||
        !compatible_arity(facts->target_previous_array_storage.size(), targets)) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "assignment target semantic arity disagrees with HIR");
    }
    if (statement.kind == StatementKind::indexed_assignment) {
      if (facts->indexed_mutation.valid() &&
          (facts->mutation_input_shape.empty() || facts->mutation_result_shape.empty() ||
           facts->mutation_input_shape.size() != facts->mutation_result_shape.size() ||
           (facts->indexed_mutation.kind == semantic::IndexedMutationKind::erase &&
            facts->indexed_mutation.axis >= facts->mutation_input_shape.size()) ||
           !semantic::valid_indexed_mutation_shapes(facts->indexed_mutation,
                                                    facts->mutation_input_shape,
                                                    facts->mutation_result_shape))) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "indexed assignment has an incomplete mutation contract");
      }
    } else if (facts->indexed_mutation.valid() || !facts->mutation_input_shape.empty() ||
               !facts->mutation_result_shape.empty()) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "non-indexed statement carries an indexed mutation contract");
    }
    const bool condition_context = source_language == SourceLanguage::matlab &&
                                   (statement.kind == StatementKind::if_statement ||
                                    statement.kind == StatementKind::while_loop);
    verify_expression(statement.expression, table, source_language, seen, stage, diagnostics,
                      condition_context);
    verify_expression(statement.secondary_expression, table, source_language, seen, stage,
                      diagnostics);
    verify_expression(statement.tertiary_expression, table, source_language, seen, stage,
                      diagnostics);
    verify_expression(statement.target_expression, table, source_language, seen, stage,
                      diagnostics);
    for (const auto& expression : statement.parameter_defaults) {
      verify_expression(expression, table, source_language, seen, stage, diagnostics);
    }
    for (const auto& selector : statement.case_selectors) {
      verify_expression(selector.lower, table, source_language, seen, stage, diagnostics);
      verify_expression(selector.upper, table, source_language, seen, stage, diagnostics);
    }
    verify_statements(statement.body, table, source_language, seen, stage, diagnostics);
    verify_statements(statement.alternative, table, source_language, seen, stage, diagnostics);
  }
}

void reindex_expression(Expression& expression, SemanticTable& source, SemanticTable& result,
                        IrIdAllocator<HirNodeId>& ids) {
  if (!expression.valid()) {
    expression.id = {};
    return;
  }
  const auto old_id = expression.id;
  auto facts = source.expression(old_id) == nullptr ? ExpressionFacts{}
                                                    : std::move(*source.expression(old_id));
  expression.id = ids.next();
  facts.origin = expression.id;
  const auto offset = result.expressions.size();
  result.nodes.push_back({SemanticNodeKind::expression, static_cast<std::uint32_t>(offset)});
  result.expressions.push_back(std::move(facts));
  for (auto& child : expression.children) reindex_expression(child, source, result, ids);
}

void reindex_statements(std::vector<Statement>& statements, SemanticTable& source,
                        SemanticTable& result, IrIdAllocator<HirNodeId>& ids) {
  for (auto& statement : statements) {
    const auto old_id = statement.id;
    auto facts = source.statement(old_id) == nullptr ? StatementFacts{}
                                                     : std::move(*source.statement(old_id));
    statement.id = ids.next();
    facts.origin = statement.id;
    const auto offset = result.statements.size();
    result.nodes.push_back({SemanticNodeKind::statement, static_cast<std::uint32_t>(offset)});
    result.statements.push_back(std::move(facts));
    reindex_expression(statement.expression, source, result, ids);
    reindex_expression(statement.secondary_expression, source, result, ids);
    reindex_expression(statement.tertiary_expression, source, result, ids);
    reindex_expression(statement.target_expression, source, result, ids);
    for (auto& expression : statement.parameter_defaults) {
      reindex_expression(expression, source, result, ids);
    }
    for (auto& selector : statement.case_selectors) {
      reindex_expression(selector.lower, source, result, ids);
      reindex_expression(selector.upper, source, result, ids);
    }
    reindex_statements(statement.body, source, result, ids);
    reindex_statements(statement.alternative, source, result, ids);
  }
}

}  // namespace

const ExpressionFacts* SemanticTable::expression(const HirNodeId id) const noexcept {
  if (!id.valid() || id.value() >= nodes.size()) return nullptr;
  const auto slot = nodes[id.value()];
  if (slot.kind != SemanticNodeKind::expression || !valid_offset(slot.offset, expressions)) {
    return nullptr;
  }
  return &expressions[slot.offset];
}

ExpressionFacts* SemanticTable::expression(const HirNodeId id) noexcept {
  return const_cast<ExpressionFacts*>(std::as_const(*this).expression(id));
}

const StatementFacts* SemanticTable::statement(const HirNodeId id) const noexcept {
  if (!id.valid() || id.value() >= nodes.size()) return nullptr;
  const auto slot = nodes[id.value()];
  if (slot.kind != SemanticNodeKind::statement || !valid_offset(slot.offset, statements)) {
    return nullptr;
  }
  return &statements[slot.offset];
}

StatementFacts* SemanticTable::statement(const HirNodeId id) noexcept {
  return const_cast<StatementFacts*>(std::as_const(*this).statement(id));
}

SemanticTable reindex_semantics(Program& program, SemanticTable&& table) {
  SemanticTable result;
  result.hir_revision = program.revision;
  result.nodes.push_back({});
  result.expressions.reserve(table.expressions.size());
  result.statements.reserve(table.statements.size());
  IrIdAllocator<HirNodeId> ids;
  reindex_statements(program.statements, table, result, ids);
  program.node_count = ids.count();
  result.hir_node_count = program.node_count;
  return result;
}

std::vector<Diagnostic> verify_semantics(const Program& program, const SemanticTable& table,
                                         const std::string_view stage) {
  std::vector<Diagnostic> diagnostics;
  if (table.hir_revision != program.revision) {
    add_error(diagnostics, {1, 1}, stage, "HIR revision is stale");
  }
  if (table.hir_node_count != program.node_count || table.nodes.size() != program.node_count + 1U) {
    add_error(diagnostics, {1, 1}, stage, "dense node index does not cover the HIR ID space");
    return diagnostics;
  }
  if (!table.nodes.empty() && table.nodes.front().kind != SemanticNodeKind::absent) {
    add_error(diagnostics, {1, 1}, stage, "node ID zero is not the required sentinel");
  }
  if (table.expressions.size() + table.statements.size() != program.node_count) {
    add_error(diagnostics, {1, 1}, stage,
              "expression and statement fact inventories do not match HIR node count");
  }
  std::vector<bool> expression_offsets(table.expressions.size(), false);
  std::vector<bool> statement_offsets(table.statements.size(), false);
  for (std::size_t index = 1; index < table.nodes.size(); ++index) {
    const auto slot = table.nodes[index];
    const auto origin = HirNodeId{static_cast<HirNodeId::value_type>(index)};
    if (slot.kind == SemanticNodeKind::expression) {
      if (!valid_offset(slot.offset, table.expressions) || expression_offsets[slot.offset] ||
          table.expressions[slot.offset].origin != origin) {
        add_error(diagnostics, {1, 1}, stage,
                  "expression fact offset is invalid, duplicated, or has the wrong origin");
      } else {
        expression_offsets[slot.offset] = true;
      }
    } else if (slot.kind == SemanticNodeKind::statement) {
      if (!valid_offset(slot.offset, table.statements) || statement_offsets[slot.offset] ||
          table.statements[slot.offset].origin != origin) {
        add_error(diagnostics, {1, 1}, stage,
                  "statement fact offset is invalid, duplicated, or has the wrong origin");
      } else {
        statement_offsets[slot.offset] = true;
      }
    } else {
      add_error(diagnostics, {1, 1}, stage, "nonzero HIR node has no semantic fact kind");
    }
  }
  std::vector<bool> seen(program.node_count + 1U, false);
  verify_statements(program.statements, table, program.language, seen, stage, diagnostics);
  for (std::size_t index = 1; index < seen.size(); ++index) {
    if (!seen[index] || table.nodes[index].kind == SemanticNodeKind::absent) {
      add_error(diagnostics, {1, 1}, stage,
                "semantic node ID space has missing or unreachable entries");
      break;
    }
  }
  return diagnostics;
}

}  // namespace mpf::detail::hir
