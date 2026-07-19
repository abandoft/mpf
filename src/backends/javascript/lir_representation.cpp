#include "lir_representation.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "backends/common/source_segments.hpp"

namespace mpf::detail::javascript {
namespace {

using AccessContext = std::vector<std::pair<std::string, lir::VariableAccess>>;

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0007", std::move(message), location});
}

lir::VariableAccess variable_access(const AccessContext& context,
                                    const std::string& name) noexcept {
  const auto found = std::find_if(context.rbegin(), context.rend(),
                                  [&](const auto& entry) { return entry.first == name; });
  return found == context.rend() ? lir::VariableAccess::direct : found->second;
}

int binary_precedence(const std::string& token) noexcept {
  if (token == "||") return 1;
  if (token == "&&") return 2;
  if (token == "===" || token == "!==" || token == "<" || token == "<=" || token == ">" ||
      token == ">=") {
    return 3;
  }
  if (token == "+" || token == "-") return 4;
  if (token == "*" || token == "/" || token == "%" || token == "//") return 5;
  if (token == "**" || token == "^" || token == ".^") return 7;
  return 10;
}

bool has_array_operand(const lir::Expression& expression) noexcept {
  return expression.broadcast.valid || expression.inferred_type == ValueType::list ||
         std::any_of(
             expression.children.begin(), expression.children.end(),
             [](const lir::Expression& child) { return child.inferred_type == ValueType::list; });
}

std::optional<semantic::IndexSelectorKind> expected_index_selector(
    const lir::Expression& selector) noexcept {
  if (selector.kind == ExpressionKind::slice) return semantic::IndexSelectorKind::slice;
  if (selector.inferred_type == ValueType::unknown) return std::nullopt;
  if (selector.inferred_type != ValueType::list) return semantic::IndexSelectorKind::scalar;
  if (std::find(selector.shape.begin(), selector.shape.end(), 0U) != selector.shape.end()) {
    return semantic::IndexSelectorKind::empty;
  }
  if (selector.element_type == ValueType::boolean) return semantic::IndexSelectorKind::logical;
  if (selector.element_type == ValueType::integer || selector.element_type == ValueType::real) {
    return semantic::IndexSelectorKind::numeric;
  }
  return std::nullopt;
}

semantic::SparseIndexKind expected_sparse_index_kind(const lir::Expression& expression) noexcept {
  if (expression.kind != ExpressionKind::index || expression.children.size() < 2U ||
      expression.children.size() > 3U ||
      expression.children.front().array_storage != ArrayStorageFormat::sparse_csc) {
    return semantic::SparseIndexKind::none;
  }
  bool scalar = true;
  for (std::size_t index = 1U; index < expression.children.size(); ++index) {
    const auto selector = expected_index_selector(expression.children[index]);
    if (!selector.has_value()) return semantic::SparseIndexKind::none;
    scalar = scalar && *selector == semantic::IndexSelectorKind::scalar;
  }
  const bool linear = expression.children.size() == 2U;
  return linear ? (scalar ? semantic::SparseIndexKind::linear_element
                          : semantic::SparseIndexKind::linear_selection)
                : (scalar ? semantic::SparseIndexKind::subscript_element
                          : semantic::SparseIndexKind::submatrix_selection);
}

semantic::SparseReshapeKind expected_sparse_reshape_kind(
    const lir::Expression& expression) noexcept {
  if (expression.kind != ExpressionKind::call || expression.children.size() < 3U) {
    return semantic::SparseReshapeKind::none;
  }
  const auto& callee = expression.children.front();
  const auto& source = expression.children[1];
  return callee.binding == BindingKind::builtin && callee.intrinsic == IntrinsicId::reshape &&
                 source.array_storage == ArrayStorageFormat::sparse_csc
             ? semantic::SparseReshapeKind::column_major_2d
             : semantic::SparseReshapeKind::none;
}

std::string matlab_solve_helper(const std::string_view operation,
                                const semantic::MatrixSolveKind solve,
                                const semantic::MatrixNumericDomain numeric_domain,
                                const semantic::MatrixFactorizationPolicy factorization_policy,
                                const semantic::MatrixStructurePolicy structure_policy,
                                const semantic::MatrixStoragePolicy storage_policy) {
  std::string suffix;
  switch (solve) {
    case semantic::MatrixSolveKind::none: return {};
    case semantic::MatrixSolveKind::square:
      if (storage_policy == semantic::MatrixStoragePolicy::sparse_csc_coefficient) {
        if (numeric_domain != semantic::MatrixNumericDomain::real ||
            factorization_policy != semantic::MatrixFactorizationPolicy::sparse_row_pivoted_lu ||
            structure_policy != semantic::MatrixStructurePolicy::classify_sparse_real_square) {
          return {};
        }
        suffix = "sparse_real_square";
        break;
      }
      if (storage_policy != semantic::MatrixStoragePolicy::dense) return {};
      if (factorization_policy != semantic::MatrixFactorizationPolicy::none) return {};
      if (structure_policy == semantic::MatrixStructurePolicy::classify_real_square) {
        suffix = "structured_real_square";
      } else if (structure_policy == semantic::MatrixStructurePolicy::classify_complex_square) {
        suffix = "structured_complex_square";
      } else {
        return {};
      }
      break;
    case semantic::MatrixSolveKind::overdetermined:
      if (storage_policy != semantic::MatrixStoragePolicy::dense ||
          factorization_policy !=
              semantic::MatrixFactorizationPolicy::rank_revealing_column_pivoted_qr ||
          structure_policy != semantic::MatrixStructurePolicy::none) {
        return {};
      }
      suffix = numeric_domain == semantic::MatrixNumericDomain::complex ? "complex_overdetermined"
                                                                        : "overdetermined";
      break;
    case semantic::MatrixSolveKind::underdetermined:
      if (storage_policy != semantic::MatrixStoragePolicy::dense ||
          factorization_policy !=
              semantic::MatrixFactorizationPolicy::rank_revealing_column_pivoted_qr ||
          structure_policy != semantic::MatrixStructurePolicy::none) {
        return {};
      }
      suffix = numeric_domain == semantic::MatrixNumericDomain::complex ? "complex_underdetermined"
                                                                        : "underdetermined";
      break;
  }
  return "__mpf_matlab_" + std::string(operation) + '_' + suffix;
}

std::string matlab_array_helper(const lir::Expression& expression) {
  const bool complex = expression.numeric_type.complexity == NumericComplexity::complex ||
                       expression.element_numeric_type.complexity == NumericComplexity::complex;
  const bool dynamic_numeric = expression.numeric_type == unknown_numeric_type ||
                               expression.element_numeric_type == unknown_numeric_type;
  switch (expression.matrix_operation.operation) {
    case semantic::MatrixOperation::multiply:
      if (expression.matrix_operation.storage_policy ==
          semantic::MatrixStoragePolicy::sparse_csc_multiply) {
        const bool left_sparse =
            expression.matrix_operation.left_storage == ArrayStorageFormat::sparse_csc;
        const bool right_sparse =
            expression.matrix_operation.right_storage == ArrayStorageFormat::sparse_csc;
        if (left_sparse && right_sparse) return "__mpf_sparse_sparse_mtimes";
        if (left_sparse) return "__mpf_sparse_dense_mtimes";
        if (right_sparse) return "__mpf_dense_sparse_mtimes";
        return {};
      }
      return expression.matrix_operation.numeric_domain == semantic::MatrixNumericDomain::complex
                 ? "__mpf_matlab_complex_mtimes"
                 : "__mpf_matlab_mtimes";
    case semantic::MatrixOperation::left_divide:
      return matlab_solve_helper(
          "mldivide", expression.matrix_operation.solve, expression.matrix_operation.numeric_domain,
          expression.matrix_operation.factorization_policy,
          expression.matrix_operation.structure_policy, expression.matrix_operation.storage_policy);
    case semantic::MatrixOperation::right_divide:
      return matlab_solve_helper(
          "mrdivide", expression.matrix_operation.solve, expression.matrix_operation.numeric_domain,
          expression.matrix_operation.factorization_policy,
          expression.matrix_operation.structure_policy, expression.matrix_operation.storage_policy);
    case semantic::MatrixOperation::integer_power:
      return expression.matrix_operation.numeric_domain == semantic::MatrixNumericDomain::complex
                 ? "__mpf_matlab_complex_mpower"
                 : "__mpf_matlab_mpower";
    case semantic::MatrixOperation::none: break;
  }
  const bool both_arrays = expression.children.size() == 2U &&
                           expression.children[0].inferred_type == ValueType::list &&
                           expression.children[1].inferred_type == ValueType::list;
  if (expression.operation == BinaryOperator::multiply && both_arrays) return {};
  if (expression.operation == BinaryOperator::add) {
    return complex           ? "__mpf_matlab_complex_add"
           : dynamic_numeric ? "__mpf_matlab_numeric_add"
                             : "__mpf_matlab_add";
  }
  if (expression.operation == BinaryOperator::subtract) {
    return complex           ? "__mpf_matlab_complex_subtract"
           : dynamic_numeric ? "__mpf_matlab_numeric_subtract"
                             : "__mpf_matlab_subtract";
  }
  if (expression.operation == BinaryOperator::multiply ||
      expression.operation == BinaryOperator::elementwise_multiply) {
    return complex           ? "__mpf_matlab_complex_multiply"
           : dynamic_numeric ? "__mpf_matlab_numeric_multiply"
                             : "__mpf_matlab_multiply";
  }
  if (expression.operation == BinaryOperator::divide ||
      expression.operation == BinaryOperator::elementwise_divide) {
    return complex           ? "__mpf_matlab_complex_divide"
           : dynamic_numeric ? "__mpf_matlab_numeric_divide"
                             : "__mpf_matlab_divide";
  }
  if (expression.operation == BinaryOperator::left_divide ||
      expression.operation == BinaryOperator::elementwise_left_divide) {
    return complex           ? "__mpf_matlab_complex_left_divide"
           : dynamic_numeric ? "__mpf_matlab_numeric_left_divide"
                             : "__mpf_matlab_left_divide";
  }
  if (expression.operation == BinaryOperator::elementwise_power) {
    return complex           ? "__mpf_matlab_complex_power"
           : dynamic_numeric ? "__mpf_matlab_numeric_power"
                             : "__mpf_matlab_power";
  }
  if (expression.operation == BinaryOperator::elementwise_logical_and) {
    return "__mpf_matlab_and";
  }
  if (expression.operation == BinaryOperator::elementwise_logical_or) {
    return "__mpf_matlab_or";
  }
  switch (expression.comparison) {
    case ComparisonOperator::equal: return "__mpf_matlab_equal";
    case ComparisonOperator::not_equal: return "__mpf_matlab_not_equal";
    case ComparisonOperator::less: return "__mpf_matlab_less";
    case ComparisonOperator::less_equal: return "__mpf_matlab_less_equal";
    case ComparisonOperator::greater: return "__mpf_matlab_greater";
    case ComparisonOperator::greater_equal: return "__mpf_matlab_greater_equal";
    case ComparisonOperator::none:
    case ComparisonOperator::identity:
    case ComparisonOperator::not_identity:
    case ComparisonOperator::contains:
    case ComparisonOperator::not_contains: break;
  }
  return {};
}

std::string scalar_division_helper(const lir::EmissionPlan& emission,
                                   const bool floor_result = false) {
  if (emission.division_by_zero != semantic::DivisionByZero::exception) return {};
  return floor_result ? "__mpf_python_floor_divide" : "__mpf_python_true_divide";
}

semantic::MatrixOperation expected_matrix_operation(const lir::Expression& expression) noexcept {
  if (expression.children.size() != 2U) return semantic::MatrixOperation::none;
  const bool left_array = expression.children[0].inferred_type == ValueType::list;
  const bool right_array = expression.children[1].inferred_type == ValueType::list;
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
    const lir::Expression& expression) noexcept {
  if (expression.children.size() != 2U) return std::nullopt;
  const auto& left = expression.children[0];
  const auto& right = expression.children[1];
  const auto left_numeric =
      left.inferred_type == ValueType::list ? left.element_numeric_type : left.numeric_type;
  const auto right_numeric =
      right.inferred_type == ValueType::list ? right.element_numeric_type : right.numeric_type;
  if (!left_numeric.present() || left_numeric.complexity == NumericComplexity::unknown) {
    return std::nullopt;
  }
  if (expression.operation == BinaryOperator::power && left.inferred_type == ValueType::list &&
      right.inferred_type != ValueType::list) {
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

bool valid_matrix_shapes(const lir::MatrixOperationPlan& plan,
                         const std::vector<std::size_t>& expression_shape) noexcept {
  if (!static_rank_two(plan.left_shape) || !static_rank_two(plan.result_shape) ||
      plan.result_shape != expression_shape ||
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
             semantic::valid_matrix_multiply_storage_contract(
                 plan.storage_policy, plan.left_storage, plan.right_storage, plan.result_storage) &&
             (plan.storage_policy != semantic::MatrixStoragePolicy::sparse_csc_multiply ||
              (plan.numeric_domain == semantic::MatrixNumericDomain::real &&
               std::find(plan.left_shape.begin(), plan.left_shape.end(), 0U) ==
                   plan.left_shape.end() &&
               std::find(plan.right_shape.begin(), plan.right_shape.end(), 0U) ==
                   plan.right_shape.end())) &&
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

std::optional<std::size_t> sparse_argument_count(const lir::Expression& expression) noexcept {
  if (expression.inferred_type != ValueType::list) {
    return expression.inferred_type == ValueType::integer ||
                   expression.inferred_type == ValueType::real
               ? std::optional<std::size_t>{1U}
               : std::nullopt;
  }
  std::size_t count = 1U;
  for (const auto extent : expression.shape) {
    if (extent == dynamic_extent ||
        (extent != 0U && count > std::numeric_limits<std::size_t>::max() / extent)) {
      return std::nullopt;
    }
    count *= extent;
  }
  return count;
}

semantic::SparseConstructionKind expected_sparse_construction_kind(
    const lir::Expression& expression) noexcept {
  if (expression.kind != ExpressionKind::call || expression.children.empty()) {
    return semantic::SparseConstructionKind::none;
  }
  const auto& callee = expression.children.front();
  if (callee.binding != BindingKind::builtin || callee.intrinsic != IntrinsicId::matlab_sparse) {
    return semantic::SparseConstructionKind::none;
  }
  switch (expression.children.size()) {
    case 2U: return semantic::SparseConstructionKind::dense_conversion;
    case 3U: return semantic::SparseConstructionKind::zero_matrix;
    case 4U: return semantic::SparseConstructionKind::triplets_inferred;
    case 6U: return semantic::SparseConstructionKind::triplets_sized;
    case 7U: return semantic::SparseConstructionKind::triplets_reserved;
    default: return semantic::SparseConstructionKind::none;
  }
}

bool valid_sparse_construction(const lir::Expression& expression) noexcept {
  const auto& sparse = expression.sparse_construction;
  if (sparse.kind != expected_sparse_construction_kind(expression)) return false;
  if (!sparse.valid()) {
    return sparse.result_shape.empty() && sparse.triplet_element_counts.empty() &&
           sparse.reserve_hint == 0U;
  }
  bool valid = expression.inferred_type == ValueType::list &&
               expression.array_storage == ArrayStorageFormat::sparse_csc &&
               sparse.result_shape == expression.shape && sparse.result_shape.size() == 2U &&
               sparse.result_shape[0] != 0U && sparse.result_shape[1] != 0U;
  const bool triplets = sparse.kind == semantic::SparseConstructionKind::triplets_inferred ||
                        sparse.kind == semantic::SparseConstructionKind::triplets_sized ||
                        sparse.kind == semantic::SparseConstructionKind::triplets_reserved;
  if (triplets) {
    valid = valid && sparse.triplet_element_counts.size() == 3U && expression.children.size() >= 4U;
    for (std::size_t index = 0U; valid && index < 3U; ++index) {
      const auto count = sparse_argument_count(expression.children[index + 1U]);
      valid = count.has_value() && *count == sparse.triplet_element_counts[index];
    }
  } else {
    valid = valid && sparse.triplet_element_counts.empty();
  }
  if (sparse.kind == semantic::SparseConstructionKind::dense_conversion) {
    valid = valid && expression.children.size() == 2U &&
            expression.children[1].shape == sparse.result_shape;
  }
  return valid && (sparse.kind == semantic::SparseConstructionKind::triplets_reserved ||
                   sparse.reserve_hint == 0U);
}

bool valid_sparse_reshape(const lir::Expression& expression) noexcept {
  const auto& sparse = expression.sparse_reshape;
  if (sparse.kind != expected_sparse_reshape_kind(expression)) return false;
  if (!sparse.valid()) {
    return sparse.dimension_form == semantic::SparseReshapeDimensionForm::none &&
           sparse.inference == semantic::SparseReshapeInference::none &&
           sparse.inferred_axis == 0U && sparse.source_storage == ArrayStorageFormat::none &&
           sparse.result_storage == ArrayStorageFormat::none && sparse.input_shape.empty() &&
           sparse.requested_shape.empty() && sparse.result_shape.empty();
  }
  const auto& source = expression.children[1];
  const auto expected_form = expression.children.size() == 3U
                                 ? semantic::SparseReshapeDimensionForm::size_vector
                                 : semantic::SparseReshapeDimensionForm::dimension_list;
  std::size_t empty_dimensions = 0U;
  std::size_t empty_axis = 0U;
  if (expected_form == semantic::SparseReshapeDimensionForm::dimension_list) {
    for (std::size_t child = 2U; child < expression.children.size(); ++child) {
      const auto& dimension = expression.children[child];
      if (dimension.kind == ExpressionKind::list && dimension.children.empty()) {
        empty_axis = child - 2U;
        ++empty_dimensions;
      }
    }
  }
  const auto expected_inference = empty_dimensions == 1U
                                      ? semantic::SparseReshapeInference::one_dimension
                                      : semantic::SparseReshapeInference::none;
  return source.inferred_type == ValueType::list && source.element_type == ValueType::real &&
         source.element_numeric_type == real_numeric_type &&
         expression.inferred_type == ValueType::list &&
         expression.element_type == ValueType::real &&
         expression.element_numeric_type == real_numeric_type && expression.column_major &&
         sparse.dimension_form == expected_form && empty_dimensions <= 1U &&
         sparse.inference == expected_inference &&
         (expected_inference == semantic::SparseReshapeInference::none ||
          sparse.inferred_axis == empty_axis) &&
         sparse.input_shape == source.shape && sparse.source_storage == source.array_storage &&
         sparse.result_shape == expression.shape &&
         sparse.result_storage == expression.array_storage &&
         semantic::valid_sparse_reshape_contract(
             sparse.kind, sparse.dimension_form, sparse.inference, sparse.inferred_axis,
             sparse.source_storage, sparse.result_storage, sparse.input_shape,
             sparse.requested_shape, sparse.result_shape);
}

bool valid_broadcast_plan(const lir::Expression& expression) noexcept {
  const auto& plan = expression.broadcast;
  if (!plan.valid) {
    return plan.shape_source == semantic::BroadcastShapeSource::static_extents &&
           plan.left_shape.empty() && plan.right_shape.empty() && plan.result_shape.empty() &&
           plan.axes.empty();
  }
  if (expression.kind != ExpressionKind::binary ||
      expression.array_operation != semantic::ArrayOperation::matlab ||
      expression.shape != plan.result_shape || plan.left_shape.size() != plan.axes.size() ||
      plan.right_shape.size() != plan.axes.size() || plan.result_shape.size() != plan.axes.size()) {
    return false;
  }
  const bool runtime = plan.shape_source == semantic::BroadcastShapeSource::runtime_operands;
  if (plan.axes.empty()) return runtime && plan.left_shape.empty() && plan.right_shape.empty();
  bool has_runtime_axis = false;
  for (std::size_t axis = 0; axis < plan.axes.size(); ++axis) {
    const auto left = plan.left_shape[axis];
    const auto right = plan.right_shape[axis];
    const auto result = plan.result_shape[axis];
    const auto mode = plan.axes[axis];
    const auto known = left == dynamic_extent ? right : left;
    const auto runtime_result = known == dynamic_extent || known == 1U ? dynamic_extent : known;
    const bool valid_axis =
        (mode == semantic::BroadcastAxis::match && left == right && result == left) ||
        (mode == semantic::BroadcastAxis::expand_left && left == 1U && result == right) ||
        (mode == semantic::BroadcastAxis::expand_right && right == 1U && result == left) ||
        (runtime && mode == semantic::BroadcastAxis::runtime &&
         (left == dynamic_extent || right == dynamic_extent) && result == runtime_result);
    if (!valid_axis) return false;
    has_runtime_axis = has_runtime_axis || mode == semantic::BroadcastAxis::runtime;
  }
  return runtime == has_runtime_axis;
}

lir::ComparisonPlan comparison_plan(const ComparisonOperator operation,
                                    const lir::EmissionPlan& emission) {
  lir::ComparisonPlan result;
  switch (operation) {
    case ComparisonOperator::none: break;
    case ComparisonOperator::equal:
      result.form = emission.structural_equality ? lir::ComparisonForm::structural_equal
                                                 : lir::ComparisonForm::infix;
      result.token = "===";
      break;
    case ComparisonOperator::not_equal:
      result.form = emission.structural_equality ? lir::ComparisonForm::structural_not_equal
                                                 : lir::ComparisonForm::infix;
      result.token = "!==";
      break;
    case ComparisonOperator::less: result.token = "<"; break;
    case ComparisonOperator::less_equal: result.token = "<="; break;
    case ComparisonOperator::greater: result.token = ">"; break;
    case ComparisonOperator::greater_equal: result.token = ">="; break;
    case ComparisonOperator::identity: result.form = lir::ComparisonForm::identity; break;
    case ComparisonOperator::not_identity: result.form = lir::ComparisonForm::not_identity; break;
    case ComparisonOperator::contains: result.form = lir::ComparisonForm::membership; break;
    case ComparisonOperator::not_contains: result.form = lir::ComparisonForm::not_membership; break;
  }
  return result;
}

lir::CallForm call_form(const lir::Expression& expression) noexcept {
  if (expression.children.empty()) return lir::CallForm::direct;
  const auto& callee = expression.children.front();
  if (callee.kind != ExpressionKind::identifier || callee.binding != BindingKind::builtin) {
    return lir::CallForm::direct;
  }
  if (callee.intrinsic == IntrinsicId::absolute && expression.children.size() == 2 &&
      expression.children[1].numeric_type.complexity == NumericComplexity::complex) {
    return lir::CallForm::complex_absolute;
  }
  if (callee.intrinsic == IntrinsicId::python_float && expression.children.size() == 2) {
    return lir::CallForm::python_float;
  }
  if (callee.intrinsic == IntrinsicId::python_length && expression.children.size() == 2) {
    return lir::CallForm::python_length;
  }
  if (callee.intrinsic == IntrinsicId::matlab_length && expression.children.size() == 2) {
    return lir::CallForm::matlab_length;
  }
  if (callee.intrinsic == IntrinsicId::element_count && expression.children.size() == 2) {
    return lir::CallForm::element_count;
  }
  if (callee.intrinsic == IntrinsicId::logical_all && expression.reduction.valid()) {
    return lir::CallForm::matlab_all;
  }
  if (callee.intrinsic == IntrinsicId::logical_any && expression.reduction.valid()) {
    return lir::CallForm::matlab_any;
  }
  if (callee.intrinsic == IntrinsicId::sum && expression.children.size() == 2) {
    return lir::CallForm::sum;
  }
  if (callee.intrinsic == IntrinsicId::present && expression.children.size() == 2) {
    return lir::CallForm::present;
  }
  if (callee.intrinsic == IntrinsicId::reshape && expression.children.size() >= 3) {
    return expression.sparse_reshape.valid() ? lir::CallForm::matlab_sparse_reshape
                                             : lir::CallForm::reshape;
  }
  if (callee.intrinsic == IntrinsicId::matlab_sparse && expression.sparse_construction.valid()) {
    return lir::CallForm::matlab_sparse;
  }
  if (callee.intrinsic == IntrinsicId::matlab_full && expression.children.size() == 2U) {
    return lir::CallForm::matlab_full;
  }
  if (callee.intrinsic == IntrinsicId::matlab_is_sparse && expression.children.size() == 2U) {
    return lir::CallForm::matlab_is_sparse;
  }
  if (callee.intrinsic == IntrinsicId::matlab_nonzero_count && expression.children.size() == 2U) {
    return lir::CallForm::matlab_nonzero_count;
  }
  return lir::CallForm::direct;
}

lir::ExpressionPlan expected_expression_plan(const lir::Expression& expression,
                                             const lir::EmissionPlan& emission,
                                             const AccessContext& context,
                                             const SourceLanguage source_language) {
  lir::ExpressionPlan result;
  if (!expression.valid()) return result;
  result.valid = true;
  result.string_value = expression.inferred_type == ValueType::string;
  switch (expression.kind) {
    case ExpressionKind::invalid: break;
    case ExpressionKind::omitted_argument:
      result.form = lir::ExpressionForm::omitted;
      result.token = "undefined";
      break;
    case ExpressionKind::end_index:
      result.form = lir::ExpressionForm::runtime_extent;
      result.token = "__mpf_extent";
      break;
    case ExpressionKind::identifier:
      result.form = expression.binding == BindingKind::builtin ? lir::ExpressionForm::target_symbol
                                                               : lir::ExpressionForm::variable;
      result.token = expression.binding == BindingKind::builtin
                         ? std::string(expression.target_binding.code)
                         : expression.value;
      if (result.form == lir::ExpressionForm::variable) {
        result.variable_access = variable_access(context, expression.value);
      }
      break;
    case ExpressionKind::number_literal:
      if (expression.numeric_type.complexity == NumericComplexity::complex &&
          !expression.value.empty()) {
        result.form = lir::ExpressionForm::literal;
        result.token =
            "__mpf_complex(0, " + expression.value.substr(0, expression.value.size() - 1U) + ')';
        break;
      }
      [[fallthrough]];
    case ExpressionKind::string_literal:
    case ExpressionKind::boolean_literal:
    case ExpressionKind::null_literal:
      result.form = lir::ExpressionForm::literal;
      result.token = expression.value;
      break;
    case ExpressionKind::unary:
      if (expression.unary_operation == UnaryOperator::transpose ||
          expression.unary_operation == UnaryOperator::conjugate_transpose) {
        result.precedence = 9;
        const bool sparse_operand =
            !expression.children.empty() &&
            expression.children.front().array_storage == ArrayStorageFormat::sparse_csc;
        const bool complex_operand =
            !expression.children.empty() &&
            (expression.children.front().numeric_type.complexity == NumericComplexity::complex ||
             expression.children.front().element_numeric_type.complexity ==
                 NumericComplexity::complex);
        result.token =
            sparse_operand ? "__mpf_sparse_transpose"
            : expression.unary_operation == UnaryOperator::conjugate_transpose && complex_operand
                ? "__mpf_matlab_ctranspose"
                : "__mpf_matlab_transpose";
        result.form = sparse_operand ? lir::ExpressionForm::matlab_sparse_transpose
                                     : lir::ExpressionForm::matlab_transpose;
      } else if (source_language == SourceLanguage::matlab &&
                 expression.logical_evaluation == semantic::LogicalEvaluation::eager_elementwise &&
                 expression.unary_operation == UnaryOperator::logical_not) {
        result.precedence = 9;
        result.token = "__mpf_matlab_not";
        result.form = lir::ExpressionForm::matlab_logical_not;
      } else if (expression.numeric_type.complexity == NumericComplexity::complex &&
                 (expression.unary_operation == UnaryOperator::positive ||
                  expression.unary_operation == UnaryOperator::negative)) {
        result.precedence = 9;
        result.token = expression.unary_operation == UnaryOperator::negative
                           ? "__mpf_complex_negate"
                           : "__mpf_as_complex";
        result.form = lir::ExpressionForm::unary_runtime_call;
      } else {
        result.precedence = 6;
        result.token = expression.value;
        result.form = emission.dynamic_truthiness && expression.value == "!"
                          ? lir::ExpressionForm::unary_truthiness
                          : lir::ExpressionForm::unary_operator;
      }
      break;
    case ExpressionKind::binary:
      if (source_language == SourceLanguage::matlab &&
          expression.logical_evaluation == semantic::LogicalEvaluation::eager_elementwise) {
        result.precedence = 9;
        result.token = matlab_array_helper(expression);
        result.form = lir::ExpressionForm::matlab_logical_operation;
        result.broadcast = expression.broadcast;
        if (result.broadcast.valid &&
            result.broadcast.shape_source == semantic::BroadcastShapeSource::runtime_operands) {
          result.token += "_runtime";
        }
      } else if (source_language == SourceLanguage::matlab &&
                 expression.logical_evaluation ==
                     semantic::LogicalEvaluation::short_circuit_boolean) {
        result.precedence = expression.operation == BinaryOperator::logical_or ||
                                    expression.operation == BinaryOperator::elementwise_logical_or
                                ? 1
                                : 2;
        result.form = expression.operation == BinaryOperator::logical_or ||
                              expression.operation == BinaryOperator::elementwise_logical_or
                          ? lir::ExpressionForm::matlab_short_circuit_or
                          : lir::ExpressionForm::matlab_short_circuit_and;
      } else if (expression.array_operation == semantic::ArrayOperation::matlab &&
                 has_array_operand(expression)) {
        result.precedence = 9;
        result.token = matlab_array_helper(expression);
        result.form = lir::ExpressionForm::matlab_array_operation;
        result.broadcast = expression.broadcast;
        if (result.broadcast.valid &&
            result.broadcast.shape_source == semantic::BroadcastShapeSource::runtime_operands) {
          result.token += "_runtime";
        }
      } else if (expression.comparison != ComparisonOperator::none) {
        result.form = lir::ExpressionForm::binary_comparison;
        result.precedence = 3;
        result.comparisons.push_back(comparison_plan(expression.comparison, emission));
      } else if (emission.operand_logical_result &&
                 expression.operation == BinaryOperator::logical_and) {
        result.precedence = binary_precedence(expression.value);
        result.token = expression.value;
        result.form = lir::ExpressionForm::binary_lazy_and;
        result.evaluation = lir::EvaluationForm::lazy_arrow_thunks;
      } else if (emission.operand_logical_result &&
                 expression.operation == BinaryOperator::logical_or) {
        result.precedence = binary_precedence(expression.value);
        result.token = expression.value;
        result.form = lir::ExpressionForm::binary_lazy_or;
        result.evaluation = lir::EvaluationForm::lazy_arrow_thunks;
      } else if (expression.numeric_type.complexity == NumericComplexity::complex) {
        result.precedence = 9;
        switch (expression.operation) {
          case BinaryOperator::add: result.token = "__mpf_complex_add"; break;
          case BinaryOperator::subtract: result.token = "__mpf_complex_subtract"; break;
          case BinaryOperator::multiply:
          case BinaryOperator::elementwise_multiply: result.token = "__mpf_complex_multiply"; break;
          case BinaryOperator::divide:
          case BinaryOperator::elementwise_divide: result.token = "__mpf_complex_divide"; break;
          case BinaryOperator::left_divide:
          case BinaryOperator::elementwise_left_divide:
            result.token = "__mpf_complex_left_divide";
            break;
          case BinaryOperator::power:
          case BinaryOperator::elementwise_power: result.token = "__mpf_complex_power"; break;
          default: break;
        }
        result.form = lir::ExpressionForm::binary_runtime_call;
      } else if (source_language == SourceLanguage::matlab &&
                 expression.numeric_type == unknown_numeric_type) {
        result.precedence = 9;
        switch (expression.operation) {
          case BinaryOperator::add: result.token = "__mpf_numeric_add"; break;
          case BinaryOperator::subtract: result.token = "__mpf_numeric_subtract"; break;
          case BinaryOperator::multiply:
          case BinaryOperator::elementwise_multiply: result.token = "__mpf_numeric_multiply"; break;
          case BinaryOperator::divide:
          case BinaryOperator::elementwise_divide: result.token = "__mpf_numeric_divide"; break;
          case BinaryOperator::left_divide:
          case BinaryOperator::elementwise_left_divide:
            result.token = "__mpf_numeric_left_divide";
            break;
          case BinaryOperator::power:
          case BinaryOperator::elementwise_power: result.token = "__mpf_numeric_power"; break;
          default: break;
        }
        result.form = lir::ExpressionForm::binary_runtime_call;
      } else if (expression.operation == BinaryOperator::floor_divide) {
        result.precedence = 9;
        result.token = scalar_division_helper(emission, true);
        result.form = lir::ExpressionForm::binary_runtime_call;
      } else if ((expression.operation == BinaryOperator::divide ||
                  expression.operation == BinaryOperator::elementwise_divide) &&
                 emission.division_by_zero == semantic::DivisionByZero::exception) {
        result.precedence = 9;
        result.token = scalar_division_helper(emission);
        result.form = lir::ExpressionForm::binary_runtime_call;
      } else if ((expression.operation == BinaryOperator::elementwise_left_divide ||
                  expression.operation == BinaryOperator::left_divide) &&
                 expression.numeric_type.complexity != NumericComplexity::complex) {
        result.precedence = 5;
        result.token = "/";
        result.form = lir::ExpressionForm::binary_reverse_divide;
      } else {
        const auto token = expression.operation == BinaryOperator::elementwise_multiply ? "*"
                           : expression.operation == BinaryOperator::elementwise_divide ? "/"
                           : expression.operation == BinaryOperator::elementwise_power ||
                                   expression.operation == BinaryOperator::power
                               ? "**"
                               : expression.value;
        result.precedence = binary_precedence(token);
        result.token = token;
        result.form = lir::ExpressionForm::binary_operator;
      }
      break;
    case ExpressionKind::comparison_chain:
      result.form = lir::ExpressionForm::comparison_chain;
      result.evaluation = lir::EvaluationForm::comparison_arrow_iife;
      result.precedence = 3;
      result.comparisons.reserve(expression.comparisons.size());
      for (const auto operation : expression.comparisons) {
        result.comparisons.push_back(comparison_plan(operation, emission));
      }
      break;
    case ExpressionKind::conditional:
      result.form = lir::ExpressionForm::conditional;
      result.precedence = 0;
      break;
    case ExpressionKind::call:
      result.form = lir::ExpressionForm::call;
      result.precedence = 9;
      result.call = call_form(expression);
      if (result.call == lir::CallForm::matlab_all || result.call == lir::CallForm::matlab_any) {
        result.reduction = expression.reduction;
      }
      if (result.call == lir::CallForm::reshape && expression.children.size() > 1U) {
        result.result_shape = expression.shape;
      }
      if (result.call == lir::CallForm::matlab_sparse_reshape) {
        result.sparse_reshape = expression.sparse_reshape;
      }
      result.call_value = expression.multi_output_call && expression.requested_outputs == 1
                              ? lir::CallValueForm::first_result
                              : lir::CallValueForm::direct;
      result.call_arguments.reserve(expression.argument_transfers.size());
      for (std::size_t index = 0; index < expression.argument_transfers.size(); ++index) {
        const auto transfer = expression.argument_transfers[index];
        lir::CallArgumentPlan argument;
        if (argument_transfer_forwards_optional(transfer)) {
          argument.form = lir::CallArgumentForm::forward_optional;
        } else if (argument_transfer_writes(transfer)) {
          const auto scalar_out = (transfer == ArgumentTransfer::mutable_borrow_out ||
                                   transfer == ArgumentTransfer::copy_out) &&
                                  index + 1U < expression.children.size() &&
                                  expression.children[index + 1U].inferred_type != ValueType::list;
          argument.form = scalar_out ? lir::CallArgumentForm::reference_box_uninitialized
                                     : lir::CallArgumentForm::reference_box;
          if (index + 1U < expression.children.size()) {
            const auto& actual = expression.children[index + 1U].plan;
            argument.writeback =
                actual.form != lir::ExpressionForm::index ? lir::WritebackForm::direct
                : actual.index == lir::IndexForm::section ? lir::WritebackForm::section
                                                          : lir::WritebackForm::element;
          }
          result.evaluation = lir::EvaluationForm::writable_call_arrow_iife;
        }
        result.call_arguments.push_back(argument);
      }
      break;
    case ExpressionKind::index:
      result.precedence = 9;
      result.index_selectors = expression.index_selectors;
      result.index_extents = expression.index_extents;
      if (expression.sparse_index.valid()) {
        result.form = lir::ExpressionForm::matlab_sparse_index;
        result.sparse_index = expression.sparse_index;
        result.index_base = expression.index_base;
        result.allow_negative_index = expression.allow_negative_index;
        result.column_major = expression.column_major;
        break;
      }
      result.form = lir::ExpressionForm::index;
      result.index = std::any_of(result.index_selectors.begin(), result.index_selectors.end(),
                                 semantic::selector_preserves_dimension)
                         ? lir::IndexForm::section
                         : lir::IndexForm::element;
      if (result.index == lir::IndexForm::section) result.result_shape = expression.shape;
      result.index_base = expression.index_base;
      result.allow_negative_index = expression.allow_negative_index;
      result.column_major = expression.column_major;
      break;
    case ExpressionKind::slice:
      result.form = lir::ExpressionForm::slice;
      result.index_base = expression.index_base;
      result.allow_negative_index = expression.allow_negative_index;
      result.inclusive_slice_stop = expression.slice_stop_inclusive;
      break;
    case ExpressionKind::member:
      result.form = lir::ExpressionForm::member;
      result.precedence = 9;
      result.token = expression.value;
      break;
    case ExpressionKind::list:
      result.form = lir::ExpressionForm::array;
      result.array_literal.form =
          source_language == SourceLanguage::matlab && expression.children.empty()
              ? lir::ArrayLiteralForm::shaped_empty
              : lir::ArrayLiteralForm::direct;
      result.array_literal.shape = expression.shape;
      break;
    case ExpressionKind::tuple: result.form = lir::ExpressionForm::tuple; break;
  }
  return result;
}

bool same_comparison(const lir::ComparisonPlan& left, const lir::ComparisonPlan& right) noexcept {
  return left.form == right.form && left.token == right.token;
}

bool same_call_argument(const lir::CallArgumentPlan& left,
                        const lir::CallArgumentPlan& right) noexcept {
  return left.form == right.form && left.writeback == right.writeback;
}

bool same_plan(const lir::ExpressionPlan& left, const lir::ExpressionPlan& right) noexcept {
  if (left.valid != right.valid || left.form != right.form || left.precedence != right.precedence ||
      left.token != right.token || left.comparisons.size() != right.comparisons.size() ||
      left.broadcast.valid != right.broadcast.valid ||
      left.broadcast.shape_source != right.broadcast.shape_source ||
      left.broadcast.left_shape != right.broadcast.left_shape ||
      left.broadcast.right_shape != right.broadcast.right_shape ||
      left.broadcast.result_shape != right.broadcast.result_shape ||
      left.broadcast.axes != right.broadcast.axes ||
      left.reduction.operation != right.reduction.operation ||
      left.reduction.axis_policy != right.reduction.axis_policy ||
      left.reduction.shape_source != right.reduction.shape_source ||
      left.reduction.input_shape != right.reduction.input_shape ||
      left.reduction.result_shape != right.reduction.result_shape ||
      left.reduction.output_shape != right.reduction.output_shape ||
      left.reduction.axes != right.reduction.axes ||
      left.reduction.scalar_result != right.reduction.scalar_result ||
      left.sparse_index.kind != right.sparse_index.kind ||
      left.sparse_index.source_storage != right.sparse_index.source_storage ||
      left.sparse_index.result_storage != right.sparse_index.result_storage ||
      left.sparse_index.input_shape != right.sparse_index.input_shape ||
      left.sparse_index.result_shape != right.sparse_index.result_shape ||
      left.sparse_reshape.kind != right.sparse_reshape.kind ||
      left.sparse_reshape.dimension_form != right.sparse_reshape.dimension_form ||
      left.sparse_reshape.inference != right.sparse_reshape.inference ||
      left.sparse_reshape.inferred_axis != right.sparse_reshape.inferred_axis ||
      left.sparse_reshape.source_storage != right.sparse_reshape.source_storage ||
      left.sparse_reshape.result_storage != right.sparse_reshape.result_storage ||
      left.sparse_reshape.input_shape != right.sparse_reshape.input_shape ||
      left.sparse_reshape.requested_shape != right.sparse_reshape.requested_shape ||
      left.sparse_reshape.result_shape != right.sparse_reshape.result_shape ||
      left.call != right.call || left.evaluation != right.evaluation ||
      left.call_value != right.call_value ||
      left.call_arguments.size() != right.call_arguments.size() || left.index != right.index ||
      left.index_selectors != right.index_selectors || left.index_extents != right.index_extents ||
      left.variable_access != right.variable_access || left.index_base != right.index_base ||
      left.allow_negative_index != right.allow_negative_index ||
      left.column_major != right.column_major ||
      left.inclusive_slice_stop != right.inclusive_slice_stop ||
      left.string_value != right.string_value || left.result_shape != right.result_shape ||
      left.array_literal.form != right.array_literal.form ||
      left.array_literal.shape != right.array_literal.shape) {
    return false;
  }
  for (std::size_t index = 0; index < left.comparisons.size(); ++index) {
    if (!same_comparison(left.comparisons[index], right.comparisons[index])) return false;
  }
  for (std::size_t index = 0; index < left.call_arguments.size(); ++index) {
    if (!same_call_argument(left.call_arguments[index], right.call_arguments[index])) return false;
  }
  return true;
}

void plan_expression(lir::Expression& expression, const lir::EmissionPlan& emission,
                     const AccessContext& context, const SourceLanguage source_language) {
  if (!expression.valid()) return;
  for (auto& child : expression.children) {
    plan_expression(child, emission, context, source_language);
  }
  expression.plan = expected_expression_plan(expression, emission, context, source_language);
}

void collect_index_extent(const lir::Expression& expression,
                          std::optional<semantic::IndexExtentSource>& source, bool& valid) {
  if (expression.kind == ExpressionKind::end_index) {
    if (!semantic::requires_runtime_extent(expression.index_extent) ||
        (source.has_value() && *source != expression.index_extent)) {
      valid = false;
      return;
    }
    source = expression.index_extent;
    return;
  }
  for (const auto& child : expression.children) collect_index_extent(child, source, valid);
}

bool logical_composition(const BinaryOperator operation) noexcept {
  return operation == BinaryOperator::logical_and || operation == BinaryOperator::logical_or ||
         operation == BinaryOperator::elementwise_logical_and ||
         operation == BinaryOperator::elementwise_logical_or;
}

semantic::LogicalEvaluation expected_logical_evaluation(const lir::Expression& expression,
                                                        const SourceLanguage source_language,
                                                        const bool condition_context) noexcept {
  if (source_language == SourceLanguage::matlab && expression.kind == ExpressionKind::unary &&
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

semantic::ReductionOperation expected_reduction_operation(
    const lir::Expression& expression, const SourceLanguage source_language) noexcept {
  if (source_language != SourceLanguage::matlab || expression.kind != ExpressionKind::call ||
      expression.children.empty()) {
    return semantic::ReductionOperation::none;
  }
  const auto& callee = expression.children.front();
  if (callee.binding != BindingKind::builtin) return semantic::ReductionOperation::none;
  if (callee.intrinsic == IntrinsicId::logical_all) {
    return semantic::ReductionOperation::logical_all;
  }
  if (callee.intrinsic == IntrinsicId::logical_any) {
    return semantic::ReductionOperation::logical_any;
  }
  return semantic::ReductionOperation::none;
}

void verify_expression(const lir::Expression& expression, const lir::EmissionPlan& emission,
                       const AccessContext& context, const SourceLanguage source_language,
                       std::vector<Diagnostic>& diagnostics, const bool condition_context = false) {
  if (!expression.valid()) return;
  if (expression.logical_evaluation !=
      expected_logical_evaluation(expression, source_language, condition_context)) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR logical evaluation policy is inconsistent");
  }
  const auto expected_reduction = expected_reduction_operation(expression, source_language);
  if (expression.reduction.operation != expected_reduction) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR logical reduction identity is inconsistent");
  } else if (expression.reduction.valid()) {
    const auto& reduction = expression.reduction;
    const bool valid_type =
        reduction.scalar_result
            ? expression.inferred_type == ValueType::boolean && expression.shape.empty()
            : expression.inferred_type == ValueType::list &&
                  expression.element_type == ValueType::boolean;
    if (!valid_type || expression.shape != reduction.output_shape ||
        !semantic::valid_logical_reduction_contract(reduction.operation, reduction.axis_policy,
                                                    reduction.shape_source, reduction.input_shape,
                                                    reduction.result_shape, reduction.output_shape,
                                                    reduction.axes, reduction.scalar_result)) {
      add_error(diagnostics, expression.location,
                "JavaScript LIR logical reduction contract is inconsistent");
    }
  } else if (expression.reduction.axis_policy != semantic::ReductionAxisPolicy::none ||
             expression.reduction.shape_source != semantic::ReductionShapeSource::static_extents ||
             !expression.reduction.input_shape.empty() ||
             !expression.reduction.result_shape.empty() ||
             !expression.reduction.output_shape.empty() || !expression.reduction.axes.empty() ||
             expression.reduction.scalar_result) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR inactive logical reduction retains state");
  }
  const auto expected_sparse_index = expected_sparse_index_kind(expression);
  if (expression.sparse_index.kind != expected_sparse_index) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR sparse-index identity is inconsistent");
  } else if (expression.sparse_index.valid()) {
    const auto& source = expression.children.front();
    const auto scalar = semantic::sparse_index_returns_scalar(expression.sparse_index.kind);
    if (source.element_type != ValueType::real ||
        source.element_numeric_type != real_numeric_type ||
        expression.sparse_index.input_shape != source.shape ||
        expression.sparse_index.source_storage != source.array_storage ||
        expression.sparse_index.result_shape != expression.shape ||
        expression.sparse_index.result_storage != expression.array_storage ||
        !semantic::valid_sparse_index_contract(
            expression.sparse_index.kind, expression.sparse_index.source_storage,
            expression.sparse_index.result_storage, expression.sparse_index.input_shape,
            expression.sparse_index.result_shape, expression.children.size() - 1U) ||
        (scalar ? expression.inferred_type != source.element_type
                : expression.inferred_type != ValueType::list ||
                      expression.element_type != source.element_type)) {
      add_error(diagnostics, expression.location,
                "JavaScript LIR sparse-index contract is inconsistent");
    }
  } else if (expression.sparse_index.source_storage != ArrayStorageFormat::none ||
             expression.sparse_index.result_storage != ArrayStorageFormat::none ||
             !expression.sparse_index.input_shape.empty() ||
             !expression.sparse_index.result_shape.empty()) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR inactive sparse-index retains state");
  }
  if ((expression.kind == ExpressionKind::end_index) !=
      semantic::requires_runtime_extent(expression.index_extent)) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR runtime index-extent ownership is inconsistent");
  }
  if (expression.kind == ExpressionKind::index) {
    if (expression.index_selectors.size() + 1U != expression.children.size() ||
        expression.index_extents.size() != expression.index_selectors.size()) {
      add_error(diagnostics, expression.location,
                "JavaScript LIR index selector or extent arity is inconsistent");
    } else {
      for (std::size_t index = 0; index < expression.index_selectors.size(); ++index) {
        const auto expected = expected_index_selector(expression.children[index + 1U]);
        if (expected.has_value() && *expected != expression.index_selectors[index]) {
          add_error(diagnostics, expression.location,
                    "JavaScript LIR index selector identity is inconsistent");
          break;
        }
        std::optional<semantic::IndexExtentSource> extent;
        bool valid_extent = true;
        collect_index_extent(expression.children[index + 1U], extent, valid_extent);
        if (!valid_extent ||
            expression.index_extents[index] != extent.value_or(semantic::IndexExtentSource::none)) {
          add_error(diagnostics, expression.location,
                    "JavaScript LIR runtime index-extent plan is inconsistent");
          break;
        }
      }
    }
  } else if (!expression.index_selectors.empty() || !expression.index_extents.empty()) {
    add_error(diagnostics, expression.location,
              "JavaScript non-index LIR expression retains selector or extent facts");
  }
  const bool requires_matlab_array_operation =
      source_language == SourceLanguage::matlab && expression.kind == ExpressionKind::binary &&
      (expression.inferred_type == ValueType::list ||
       (expression.inferred_type == ValueType::unknown && expression.broadcast.valid &&
        expression.broadcast.shape_source == semantic::BroadcastShapeSource::runtime_operands));
  if ((expression.array_operation == semantic::ArrayOperation::matlab) !=
      requires_matlab_array_operation) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR Matlab array-operation identity is inconsistent");
  }
  if (!valid_broadcast_plan(expression)) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR Matlab broadcast plan is inconsistent");
  }
  const auto expected_matrix = source_language == SourceLanguage::matlab
                                   ? expected_matrix_operation(expression)
                                   : semantic::MatrixOperation::none;
  const auto expected_matrix_domain = expected_matrix_numeric_domain(expression);
  if (expression.matrix_operation.operation != expected_matrix ||
      (!expression.matrix_operation.valid() &&
       (expression.matrix_operation.solve != semantic::MatrixSolveKind::none ||
        expression.matrix_operation.numeric_domain != semantic::MatrixNumericDomain::none ||
        expression.matrix_operation.condition_policy != semantic::MatrixConditionPolicy::none ||
        expression.matrix_operation.factorization_policy !=
            semantic::MatrixFactorizationPolicy::none ||
        expression.matrix_operation.structure_policy != semantic::MatrixStructurePolicy::none ||
        expression.matrix_operation.storage_policy != semantic::MatrixStoragePolicy::none ||
        expression.matrix_operation.left_storage != ArrayStorageFormat::none ||
        expression.matrix_operation.right_storage != ArrayStorageFormat::none ||
        expression.matrix_operation.result_storage != ArrayStorageFormat::none ||
        !expression.matrix_operation.left_shape.empty() ||
        !expression.matrix_operation.right_shape.empty() ||
        !expression.matrix_operation.result_shape.empty())) ||
      (expression.matrix_operation.valid() &&
       (expression.array_operation != semantic::ArrayOperation::matlab ||
        expression.broadcast.valid || !expected_matrix_domain.has_value() ||
        expression.matrix_operation.numeric_domain != *expected_matrix_domain ||
        expression.children.size() != 2U ||
        expression.matrix_operation.left_storage != expression.children[0].array_storage ||
        expression.matrix_operation.right_storage != expression.children[1].array_storage ||
        expression.matrix_operation.result_storage != expression.array_storage ||
        !valid_matrix_shapes(expression.matrix_operation, expression.shape)))) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR Matlab matrix-operation plan is inconsistent");
  }
  if ((expression.plan.form == lir::ExpressionForm::binary_runtime_call ||
       expression.plan.form == lir::ExpressionForm::unary_runtime_call) &&
      expression.plan.token.empty()) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR numeric runtime plan has no target helper");
  }
  if (!valid_sparse_construction(expression)) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR sparse-construction plan is inconsistent");
  }
  if (!valid_sparse_reshape(expression)) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR sparse-reshape plan is inconsistent");
  }
  if (!same_plan(expression.plan,
                 expected_expression_plan(expression, emission, context, source_language))) {
    add_error(diagnostics, expression.location,
              "JavaScript LIR expression representation plan is inconsistent");
  }
  const bool child_condition = condition_context && expression.kind == ExpressionKind::binary &&
                               logical_composition(expression.operation);
  for (const auto& child : expression.children) {
    verify_expression(child, emission, context, source_language, diagnostics, child_condition);
  }
}

lir::SelectorForm selector_form(const lir::CaseSelector& selector) noexcept {
  if (!selector.range) return lir::SelectorForm::value;
  if (selector.has_lower && selector.has_upper) return lir::SelectorForm::closed_range;
  return selector.has_lower ? lir::SelectorForm::lower_bound : lir::SelectorForm::upper_bound;
}

std::vector<lir::AssignmentLeafPlan> assignment_leaves(const AssignmentPattern& pattern,
                                                       const AccessContext& context) {
  std::vector<const AssignmentPattern*> leaves;
  collect_assignment_leaves(pattern, leaves);
  std::vector<lir::AssignmentLeafPlan> result;
  result.reserve(leaves.size());
  for (const auto* leaf : leaves) {
    lir::AssignmentLeafPlan plan;
    plan.name = leaf->name;
    plan.access = variable_access(context, leaf->name);
    plan.captured_sequence = leaf->kind != AssignmentPatternKind::name;
    plan.access_path = leaf->access_path;
    plan.captured_paths = leaf->captured_paths;
    result.push_back(std::move(plan));
  }
  return result;
}

lir::StatementPlan expected_statement_plan(const lir::Statement& statement,
                                           const lir::EmissionPlan& emission,
                                           const AccessContext& context) {
  lir::StatementPlan result;
  result.valid = true;
  switch (statement.kind) {
    case StatementKind::declaration:
      result.target_access = variable_access(context, statement.name);
      if (statement.dummy_parameter) {
        result.form = lir::StatementForm::discard;
      } else if (statement.has_expression) {
        result.form = lir::StatementForm::declaration_initializer;
      } else if (statement.declared_type == ValueType::list && !statement.shape.empty()) {
        result.form = lir::StatementForm::declaration_array;
        result.array_shape = statement.shape;
        result.array_default = statement.element_type == ValueType::boolean  ? "false"
                               : statement.element_type == ValueType::string ? "\"\""
                                                                             : "0";
      }
      break;
    case StatementKind::assignment:
      result.form = lir::StatementForm::assignment;
      result.target_access = variable_access(context, statement.name);
      break;
    case StatementKind::multi_assignment:
      if (statement.has_target_pattern) {
        result.form = lir::StatementForm::multi_pattern;
        result.assignment_leaves = assignment_leaves(statement.target_pattern, context);
      } else {
        result.form = lir::StatementForm::multi_destructure;
        result.targets = statement.target_names;
        result.target_accesses.reserve(result.targets.size());
        for (const auto& name : result.targets) {
          result.target_accesses.push_back(variable_access(context, name));
        }
      }
      break;
    case StatementKind::indexed_assignment:
      result.form = statement.target_expression.plan.index == lir::IndexForm::section
                        ? lir::StatementForm::indexed_section_assignment
                        : lir::StatementForm::indexed_element_assignment;
      result.resizable_section =
          result.form == lir::StatementForm::indexed_section_assignment &&
          statement.indexed_mutation.kind == semantic::IndexedMutationKind::resize;
      result.indexed_mutation = statement.indexed_mutation;
      result.mutation_input_shape = statement.mutation_input_shape;
      result.mutation_result_shape = statement.mutation_result_shape;
      result.sparse_mutation = statement.sparse_mutation;
      if (statement.indexed_mutation.kind == semantic::IndexedMutationKind::grow) {
        result.array_default = statement.element_type == ValueType::boolean  ? "false"
                               : statement.element_type == ValueType::string ? "\"\""
                                                                             : "0";
      }
      break;
    case StatementKind::print:
      result.form = !statement.has_expression ? lir::StatementForm::print_empty
                    : statement.expression.plan.form == lir::ExpressionForm::tuple
                        ? lir::StatementForm::print_tuple
                        : lir::StatementForm::print_value;
      break;
    case StatementKind::return_statement:
      result.form = statement.has_expression ? lir::StatementForm::return_value
                                             : lir::StatementForm::return_void;
      break;
    case StatementKind::break_statement: result.form = lir::StatementForm::break_loop; break;
    case StatementKind::continue_statement: result.form = lir::StatementForm::continue_loop; break;
    case StatementKind::expression:
      result.form = statement.expression.valid() ? lir::StatementForm::expression
                                                 : lir::StatementForm::discard;
      break;
    case StatementKind::if_statement:
      result.form = lir::StatementForm::conditional;
      result.condition = emission.dynamic_truthiness  ? lir::ConditionForm::runtime_truthy
                         : emission.matlab_truthiness ? lir::ConditionForm::matlab_all_nonzero
                                                      : lir::ConditionForm::direct;
      result.has_alternative = !statement.alternative.empty();
      break;
    case StatementKind::select_case:
      result.form = lir::StatementForm::selection;
      result.character_selector =
          statement.expression.plan.string_value && emission.padded_character_selection;
      break;
    case StatementKind::case_clause:
      result.form = lir::StatementForm::case_clause;
      if (!statement.default_case) {
        result.selectors.reserve(statement.case_selectors.size());
        for (const auto& selector : statement.case_selectors) {
          result.selectors.push_back(selector_form(selector));
        }
      }
      break;
    case StatementKind::while_loop:
      result.form = lir::StatementForm::while_loop;
      result.condition = emission.dynamic_truthiness  ? lir::ConditionForm::runtime_truthy
                         : emission.matlab_truthiness ? lir::ConditionForm::matlab_all_nonzero
                                                      : lir::ConditionForm::direct;
      result.has_alternative = !statement.alternative.empty();
      break;
    case StatementKind::range_loop:
      result.form = lir::StatementForm::range_loop;
      result.target_access = variable_access(context, statement.name);
      result.has_alternative = !statement.alternative.empty();
      result.range_has_step = statement.has_tertiary_expression;
      result.retain_loop_value = statement.retain_last_loop_value;
      result.inclusive_stop = statement.inclusive_stop;
      break;
    case StatementKind::for_loop:
      result.form = lir::StatementForm::for_loop;
      result.condition = lir::ConditionForm::direct;
      result.target_access = variable_access(context, statement.name);
      break;
    case StatementKind::function:
      result.form = lir::StatementForm::function;
      result.parameter_defaults.reserve(statement.parameter_defaults.size());
      for (const auto& default_value : statement.parameter_defaults) {
        result.parameter_defaults.push_back(emission.emit_parameter_defaults &&
                                            default_value.valid());
      }
      result.return_names = statement.return_names;
      break;
  }
  return result;
}

bool same_access_path(const std::vector<AssignmentAccess>& left,
                      const std::vector<AssignmentAccess>& right) noexcept {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].index != right[index].index || left[index].list != right[index].list) {
      return false;
    }
  }
  return true;
}

bool same_assignment_leaf(const lir::AssignmentLeafPlan& left,
                          const lir::AssignmentLeafPlan& right) noexcept {
  if (left.name != right.name || left.access != right.access ||
      left.captured_sequence != right.captured_sequence ||
      !same_access_path(left.access_path, right.access_path) ||
      left.captured_paths.size() != right.captured_paths.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.captured_paths.size(); ++index) {
    if (!same_access_path(left.captured_paths[index], right.captured_paths[index])) return false;
  }
  return true;
}

bool same_statement_plan(const lir::StatementPlan& left, const lir::StatementPlan& right) noexcept {
  if (left.valid != right.valid || left.form != right.form || left.condition != right.condition ||
      left.target_access != right.target_access || left.has_alternative != right.has_alternative ||
      left.range_has_step != right.range_has_step ||
      left.retain_loop_value != right.retain_loop_value ||
      left.inclusive_stop != right.inclusive_stop ||
      left.resizable_section != right.resizable_section ||
      left.indexed_mutation.kind != right.indexed_mutation.kind ||
      left.indexed_mutation.shape_source != right.indexed_mutation.shape_source ||
      left.indexed_mutation.linear != right.indexed_mutation.linear ||
      left.indexed_mutation.axis != right.indexed_mutation.axis ||
      left.mutation_input_shape != right.mutation_input_shape ||
      left.mutation_result_shape != right.mutation_result_shape ||
      left.sparse_mutation.kind != right.sparse_mutation.kind ||
      left.sparse_mutation.replacement != right.sparse_mutation.replacement ||
      left.sparse_mutation.duplicate_policy != right.sparse_mutation.duplicate_policy ||
      left.sparse_mutation.zero_policy != right.sparse_mutation.zero_policy ||
      left.sparse_mutation.source_storage != right.sparse_mutation.source_storage ||
      left.sparse_mutation.replacement_storage != right.sparse_mutation.replacement_storage ||
      left.sparse_mutation.result_storage != right.sparse_mutation.result_storage ||
      left.sparse_mutation.input_shape != right.sparse_mutation.input_shape ||
      left.sparse_mutation.selection_shape != right.sparse_mutation.selection_shape ||
      left.sparse_mutation.replacement_shape != right.sparse_mutation.replacement_shape ||
      left.sparse_mutation.result_shape != right.sparse_mutation.result_shape ||
      left.character_selector != right.character_selector ||
      left.array_default != right.array_default || left.array_shape != right.array_shape ||
      left.targets != right.targets || left.target_accesses != right.target_accesses ||
      left.assignment_leaves.size() != right.assignment_leaves.size() ||
      left.selectors != right.selectors || left.parameter_defaults != right.parameter_defaults ||
      left.return_names != right.return_names) {
    return false;
  }
  for (std::size_t index = 0; index < left.assignment_leaves.size(); ++index) {
    if (!same_assignment_leaf(left.assignment_leaves[index], right.assignment_leaves[index])) {
      return false;
    }
  }
  return true;
}

AccessContext function_context(const lir::Statement& statement) {
  AccessContext result;
  result.reserve(statement.parameters.size());
  for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
    const auto access =
        index < statement.function_abi.parameters.size() &&
                statement.function_abi.parameters[index] == lir::ParameterPassing::reference_box
            ? lir::VariableAccess::reference_box_value
            : lir::VariableAccess::direct;
    result.emplace_back(statement.parameters[index], access);
  }
  return result;
}

void plan_statements(std::vector<lir::Statement>& statements, const lir::EmissionPlan& emission,
                     const AccessContext& context, const SourceLanguage source_language) {
  for (auto& statement : statements) {
    const auto nested_context =
        statement.kind == StatementKind::function ? function_context(statement) : context;
    const auto& expression_context =
        statement.kind == StatementKind::function ? nested_context : context;
    plan_expression(statement.expression, emission, expression_context, source_language);
    plan_expression(statement.secondary_expression, emission, expression_context, source_language);
    plan_expression(statement.tertiary_expression, emission, expression_context, source_language);
    plan_expression(statement.target_expression, emission, expression_context, source_language);
    for (auto& expression : statement.parameter_defaults) {
      plan_expression(expression, emission, expression_context, source_language);
    }
    for (auto& selector : statement.case_selectors) {
      plan_expression(selector.lower, emission, expression_context, source_language);
      plan_expression(selector.upper, emission, expression_context, source_language);
    }
    statement.plan = expected_statement_plan(statement, emission, context);
    plan_statements(statement.body, emission, nested_context, source_language);
    plan_statements(statement.alternative, emission, nested_context, source_language);
  }
}

void verify_statements(const std::vector<lir::Statement>& statements,
                       const lir::EmissionPlan& emission, const AccessContext& context,
                       const SourceLanguage source_language, std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    const auto nested_context =
        statement.kind == StatementKind::function ? function_context(statement) : context;
    const auto& expression_context =
        statement.kind == StatementKind::function ? nested_context : context;
    const bool condition_context = source_language == SourceLanguage::matlab &&
                                   (statement.kind == StatementKind::if_statement ||
                                    statement.kind == StatementKind::while_loop);
    verify_expression(statement.expression, emission, expression_context, source_language,
                      diagnostics, condition_context);
    verify_expression(statement.secondary_expression, emission, expression_context, source_language,
                      diagnostics);
    verify_expression(statement.tertiary_expression, emission, expression_context, source_language,
                      diagnostics);
    verify_expression(statement.target_expression, emission, expression_context, source_language,
                      diagnostics);
    for (const auto& expression : statement.parameter_defaults) {
      verify_expression(expression, emission, expression_context, source_language, diagnostics);
    }
    for (const auto& selector : statement.case_selectors) {
      verify_expression(selector.lower, emission, expression_context, source_language, diagnostics);
      verify_expression(selector.upper, emission, expression_context, source_language, diagnostics);
    }
    if (statement.kind == StatementKind::indexed_assignment) {
      if (!semantic::valid_indexed_mutation_shapes(statement.indexed_mutation,
                                                   statement.mutation_input_shape,
                                                   statement.mutation_result_shape)) {
        add_error(diagnostics, {statement.line, 1},
                  "JavaScript LIR indexed mutation contract is inconsistent");
      }
      const auto& sparse = statement.sparse_mutation;
      const bool expected_sparse = statement.target_expression.sparse_index.valid();
      if (expected_sparse != sparse.valid()) {
        add_error(diagnostics, {statement.line, 1},
                  "JavaScript LIR sparse-mutation identity is inconsistent");
      } else if (sparse.valid()) {
        const bool deletion =
            statement.indexed_mutation.kind == semantic::IndexedMutationKind::erase;
        const auto expected_kind =
            deletion
                ? (statement.indexed_mutation.linear ? semantic::SparseMutationKind::linear_deletion
                                                     : semantic::SparseMutationKind::axis_deletion)
                : (statement.indexed_mutation.linear
                       ? semantic::SparseMutationKind::linear_assignment
                       : semantic::SparseMutationKind::subscript_assignment);
        const auto replacement_storage =
            deletion ? ArrayStorageFormat::none : statement.expression.array_storage;
        if (sparse.kind != expected_kind || sparse.input_shape != statement.mutation_input_shape ||
            sparse.result_shape != statement.mutation_result_shape ||
            sparse.selection_shape != statement.target_expression.sparse_index.result_shape ||
            sparse.replacement_storage != replacement_storage ||
            sparse.replacement_shape !=
                (deletion ? std::vector<std::size_t>{} : statement.expression.shape) ||
            !semantic::valid_sparse_mutation_contract(
                sparse.kind, sparse.replacement, sparse.duplicate_policy, sparse.zero_policy,
                sparse.source_storage, sparse.replacement_storage, sparse.result_storage,
                sparse.input_shape, sparse.selection_shape, sparse.replacement_shape,
                sparse.result_shape, statement.target_expression.children.size() - 1U,
                statement.indexed_mutation)) {
          add_error(diagnostics, {statement.line, 1},
                    "JavaScript LIR sparse mutation contract is inconsistent");
        }
      } else if (sparse.replacement != semantic::SparseReplacementKind::none ||
                 sparse.duplicate_policy != semantic::SparseDuplicateWritePolicy::none ||
                 sparse.zero_policy != semantic::SparseZeroWritePolicy::none ||
                 sparse.source_storage != ArrayStorageFormat::none ||
                 sparse.replacement_storage != ArrayStorageFormat::none ||
                 sparse.result_storage != ArrayStorageFormat::none || !sparse.input_shape.empty() ||
                 !sparse.selection_shape.empty() || !sparse.replacement_shape.empty() ||
                 !sparse.result_shape.empty()) {
        add_error(diagnostics, {statement.line, 1},
                  "JavaScript LIR inactive sparse mutation retains state");
      }
    } else if (statement.indexed_mutation.valid() || !statement.mutation_input_shape.empty() ||
               !statement.mutation_result_shape.empty() || statement.sparse_mutation.valid() ||
               statement.sparse_mutation.replacement != semantic::SparseReplacementKind::none ||
               statement.sparse_mutation.duplicate_policy !=
                   semantic::SparseDuplicateWritePolicy::none ||
               statement.sparse_mutation.zero_policy != semantic::SparseZeroWritePolicy::none ||
               statement.sparse_mutation.source_storage != ArrayStorageFormat::none ||
               statement.sparse_mutation.replacement_storage != ArrayStorageFormat::none ||
               statement.sparse_mutation.result_storage != ArrayStorageFormat::none ||
               !statement.sparse_mutation.input_shape.empty() ||
               !statement.sparse_mutation.selection_shape.empty() ||
               !statement.sparse_mutation.replacement_shape.empty() ||
               !statement.sparse_mutation.result_shape.empty()) {
      add_error(diagnostics, {statement.line, 1},
                "JavaScript LIR non-indexed statement carries a mutation contract");
    }
    if (!same_statement_plan(statement.plan,
                             expected_statement_plan(statement, emission, context))) {
      add_error(diagnostics, {statement.line, 1},
                "JavaScript LIR statement representation plan is inconsistent");
    }
    verify_statements(statement.body, emission, nested_context, source_language, diagnostics);
    verify_statements(statement.alternative, emission, nested_context, source_language,
                      diagnostics);
  }
}

}  // namespace

void plan_lir_representation(lir::SemanticProgram& program) {
  plan_statements(program.statements, program.emission, {}, program.source_language);
  program.source_segments = build_source_segment_plan(program.statements, program.node_count);
}

void verify_lir_representation(const lir::SemanticProgram& program,
                               std::vector<Diagnostic>& diagnostics) {
  verify_statements(program.statements, program.emission, {}, program.source_language, diagnostics);
  const auto expected = build_source_segment_plan(program.statements, program.node_count);
  if (!same_source_segment_plan(program.source_segments, expected)) {
    add_error(diagnostics, {1, 1}, "JavaScript LIR source segment plan is inconsistent");
  }
}

}  // namespace mpf::detail::javascript
