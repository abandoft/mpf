#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "analyzer_internal.hpp"

namespace mpf::detail::semantic_internal {
namespace {

std::optional<long long> integral_constant(const Expression& expression) {
  const auto value = numeric_constant(expression);
  const auto minimum = static_cast<double>(std::numeric_limits<long long>::min());
  const auto upper_exclusive = -minimum;
  if (!value.has_value() || *value < minimum || *value >= upper_exclusive) {
    return std::nullopt;
  }
  const auto integral = static_cast<long long>(*value);
  return static_cast<double>(integral) == *value ? std::optional<long long>{integral}
                                                 : std::nullopt;
}

std::optional<std::size_t> normalized_index(const long long value, const std::size_t extent,
                                            const std::size_t base, const bool allow_negative) {
  if (extent == dynamic_extent) return std::nullopt;
  if (allow_negative && value < 0) {
    const auto magnitude = static_cast<unsigned long long>(-(value + 1LL)) + 1ULL;
    if (magnitude > static_cast<unsigned long long>(extent)) return std::nullopt;
    return extent - static_cast<std::size_t>(magnitude);
  }
  if (value < 0) return std::nullopt;
  const auto unsigned_value = static_cast<unsigned long long>(value);
  if (unsigned_value < base) return std::nullopt;
  const auto result = unsigned_value - base;
  return result < extent ? std::optional<std::size_t>{static_cast<std::size_t>(result)}
                         : std::nullopt;
}

std::optional<StorageRegionDimension> scalar_region_dimension(const Expression& index,
                                                              const std::size_t extent,
                                                              const std::size_t base,
                                                              const bool allow_negative) {
  if (extent == dynamic_extent) return std::nullopt;
  const auto constant = integral_constant(index);
  if (!constant.has_value()) return std::nullopt;
  const auto normalized = normalized_index(*constant, extent, base, allow_negative);
  return normalized.has_value() ? std::optional<StorageRegionDimension>{{*normalized, 1U, 1U}}
                                : std::nullopt;
}

std::optional<StorageRegionDimension> slice_region_dimension(const Expression& slice,
                                                             const hir::ExpressionFacts& facts,
                                                             const std::size_t extent,
                                                             const std::size_t count) {
  if (slice.children.size() != 3U || extent == dynamic_extent ||
      extent > static_cast<std::size_t>(LLONG_MAX) ||
      facts.index_base > static_cast<std::size_t>(LLONG_MAX)) {
    return std::nullopt;
  }
  const auto optional_bound = [&](const Expression& bound) -> std::optional<long long> {
    return bound.valid() ? integral_constant(bound) : std::optional<long long>{};
  };
  const auto start_value = optional_bound(slice.children[0]);
  const auto stop_value = optional_bound(slice.children[1]);
  const auto step_value = optional_bound(slice.children[2]);
  if ((slice.children[0].valid() && !start_value.has_value()) ||
      (slice.children[1].valid() && !stop_value.has_value()) ||
      (slice.children[2].valid() && !step_value.has_value())) {
    return std::nullopt;
  }
  const auto step = step_value.value_or(1);
  if (step == 0 || step == LLONG_MIN) return std::nullopt;
  if (count == 0U) return StorageRegionDimension{0U, 1U, 0U};

  long long first = 0;
  if (!facts.slice_stop_inclusive) {
    const auto size = static_cast<long long>(extent);
    first = start_value.value_or(step > 0 ? 0 : size - 1);
    if (start_value.has_value() && first < 0) first += size;
    first =
        step > 0 ? std::max(0LL, std::min(first, size)) : std::max(-1LL, std::min(first, size - 1));
  } else {
    first = start_value.value_or(step > 0 ? static_cast<long long>(facts.index_base)
                                          : static_cast<long long>(extent - 1U) +
                                                static_cast<long long>(facts.index_base));
    first -= static_cast<long long>(facts.index_base);
  }
  if (first < 0) return std::nullopt;
  const auto distance = static_cast<unsigned long long>(count - 1U);
  const auto magnitude = static_cast<unsigned long long>(step > 0 ? step : -step);
  if (distance != 0U && magnitude > static_cast<unsigned long long>(LLONG_MAX) / distance) {
    return std::nullopt;
  }
  const auto offset = static_cast<long long>(distance * magnitude);
  const auto lowest = step > 0 ? first : first - offset;
  if (lowest < 0) return std::nullopt;
  return StorageRegionDimension{static_cast<std::size_t>(lowest),
                                static_cast<std::size_t>(magnitude), count};
}

std::vector<std::size_t> matlab_broadcast_shape(std::vector<std::size_t> shape,
                                                const std::size_t peer_rank) {
  // A flat Matlab literal is represented compactly as rank one in HIR, but participates in
  // compatible-size operations as a 1-by-N row vector when paired with a matrix/N-D array.
  if (shape.size() == 1U && peer_rank >= 2U) shape.insert(shape.begin(), 1U);
  return shape;
}

std::optional<hir::BroadcastPlan> matlab_broadcast_plan(const std::vector<std::size_t>& raw_left,
                                                        const std::vector<std::size_t>& raw_right) {
  auto left = matlab_broadcast_shape(raw_left, raw_right.size());
  auto right = matlab_broadcast_shape(raw_right, raw_left.size());
  const auto rank = std::max(left.size(), right.size());
  left.resize(rank, 1U);
  right.resize(rank, 1U);

  hir::BroadcastPlan result;
  result.valid = true;
  result.left_shape = left;
  result.right_shape = right;
  result.result_shape.reserve(rank);
  result.axes.reserve(rank);
  for (std::size_t axis = 0; axis < rank; ++axis) {
    const auto left_extent = left[axis];
    const auto right_extent = right[axis];
    if (left_extent == dynamic_extent || right_extent == dynamic_extent) {
      result.shape_source = semantic::BroadcastShapeSource::runtime_operands;
      const auto known = left_extent == dynamic_extent ? right_extent : left_extent;
      result.result_shape.push_back(known == dynamic_extent || known == 1U ? dynamic_extent
                                                                           : known);
      result.axes.push_back(semantic::BroadcastAxis::runtime);
    } else if (left_extent == right_extent) {
      result.result_shape.push_back(left_extent);
      result.axes.push_back(semantic::BroadcastAxis::match);
    } else if (left_extent == 1U) {
      result.result_shape.push_back(right_extent);
      result.axes.push_back(semantic::BroadcastAxis::expand_left);
    } else if (right_extent == 1U) {
      result.result_shape.push_back(left_extent);
      result.axes.push_back(semantic::BroadcastAxis::expand_right);
    } else {
      return std::nullopt;
    }
  }
  return result;
}

hir::BroadcastPlan matlab_runtime_broadcast_plan() {
  hir::BroadcastPlan result;
  result.valid = true;
  result.shape_source = semantic::BroadcastShapeSource::runtime_operands;
  return result;
}

bool static_rank_two_shape(const std::vector<std::size_t>& shape) noexcept {
  return shape.size() == 2U && std::find(shape.begin(), shape.end(), dynamic_extent) == shape.end();
}

std::optional<std::vector<std::size_t>> static_matlab_matrix_shape(
    const std::vector<std::size_t>& shape) {
  if (shape.size() == 1U && shape.front() != dynamic_extent) {
    return std::vector<std::size_t>{1U, shape.front()};
  }
  return static_rank_two_shape(shape) ? std::optional<std::vector<std::size_t>>{shape}
                                      : std::nullopt;
}

std::optional<std::size_t> static_element_count(const std::vector<std::size_t>& shape) {
  if (shape.empty()) return std::nullopt;
  std::size_t result = 1U;
  for (const auto extent : shape) {
    if (extent == dynamic_extent ||
        (extent != 0U && result > std::numeric_limits<std::size_t>::max() / extent)) {
      return std::nullopt;
    }
    result *= extent;
  }
  return result;
}

std::optional<std::size_t> boolean_literal_count(const Expression& expression) {
  if (expression.kind == ExpressionKind::boolean_literal) {
    return expression.value == "true" ? 1U : 0U;
  }
  if (expression.kind != ExpressionKind::list) return std::nullopt;
  std::size_t result = 0U;
  for (const auto& child : expression.children) {
    const auto count = boolean_literal_count(child);
    if (!count.has_value() || result > std::numeric_limits<std::size_t>::max() - *count) {
      return std::nullopt;
    }
    result += *count;
  }
  return result;
}

bool empty_list_literal(const Expression& expression) noexcept {
  return expression.kind == ExpressionKind::list && expression.children.empty();
}

bool full_slice(const Expression& expression) noexcept {
  return expression.kind == ExpressionKind::slice && expression.children.size() == 3U &&
         std::none_of(expression.children.begin(), expression.children.end(),
                      [](const Expression& child) { return child.valid(); });
}

bool collect_integral_selector_values(const Expression& expression,
                                      std::vector<long long>& values) {
  if (expression.kind == ExpressionKind::list) {
    for (const auto& child : expression.children) {
      if (!collect_integral_selector_values(child, values)) return false;
    }
    return true;
  }
  const auto value = integral_constant(expression);
  if (!value.has_value()) return false;
  values.push_back(*value);
  return true;
}

std::optional<std::size_t> selector_required_extent(const Expression& selector,
                                                    const semantic::IndexSelectorKind kind,
                                                    const std::size_t current_extent) {
  if (kind == semantic::IndexSelectorKind::empty || kind == semantic::IndexSelectorKind::logical) {
    return current_extent == dynamic_extent ? std::nullopt
                                            : std::optional<std::size_t>{current_extent};
  }
  std::vector<long long> values;
  if (kind == semantic::IndexSelectorKind::slice) {
    if (full_slice(selector)) {
      return current_extent == dynamic_extent ? std::nullopt
                                              : std::optional<std::size_t>{current_extent};
    }
    if (selector.children.size() != 3U) return std::nullopt;
    for (std::size_t position = 0; position < 2U; ++position) {
      const auto& bound = selector.children[position];
      if (!bound.valid()) continue;
      const auto value = integral_constant(bound);
      if (!value.has_value()) return std::nullopt;
      values.push_back(*value);
    }
  } else if (!collect_integral_selector_values(selector, values)) {
    return std::nullopt;
  }
  if (values.empty()) return current_extent;
  const auto maximum = *std::max_element(values.begin(), values.end());
  return maximum < 1 ? std::optional<std::size_t>{0U}
                     : std::optional<std::size_t>{static_cast<std::size_t>(maximum)};
}

std::optional<std::size_t> selector_erased_count(const Expression& selector,
                                                 const semantic::IndexSelectorKind kind,
                                                 const hir::SemanticTable& semantics,
                                                 const std::size_t current_extent) {
  switch (kind) {
    case semantic::IndexSelectorKind::scalar: return 1U;
    case semantic::IndexSelectorKind::empty: return 0U;
    case semantic::IndexSelectorKind::logical: return boolean_literal_count(selector);
    case semantic::IndexSelectorKind::slice:
      if (full_slice(selector)) return current_extent;
      if (const auto& shape = semantic(semantics, selector).shape;
          shape.size() == 1U && shape.front() != dynamic_extent) {
        return shape.front();
      }
      return std::nullopt;
    case semantic::IndexSelectorKind::numeric: {
      std::vector<long long> values;
      if (!collect_integral_selector_values(selector, values)) return std::nullopt;
      std::sort(values.begin(), values.end());
      values.erase(std::unique(values.begin(), values.end()), values.end());
      return values.size();
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> matlab_vector_axis(const std::vector<std::size_t>& shape) {
  if (shape.size() == 1U) return 0U;
  if (shape.size() != 2U) return std::nullopt;
  if (shape[0] == 1U) return 1U;
  if (shape[1] == 1U) return 0U;
  return std::nullopt;
}

}  // namespace

ValueType Analyzer::analyze_expression(Expression& expression) {
  switch (expression.kind) {
    case ExpressionKind::invalid:
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    case ExpressionKind::number_literal:
      semantic(semantics_, expression).inferred_type =
          program_.language == SourceLanguage::typescript ||
                  expression.value.find_first_of(".eE") != std::string::npos
              ? ValueType::real
              : ValueType::integer;
      return semantic(semantics_, expression).inferred_type;
    case ExpressionKind::string_literal:
      return semantic(semantics_, expression).inferred_type = ValueType::string;
    case ExpressionKind::boolean_literal:
      return semantic(semantics_, expression).inferred_type = ValueType::boolean;
    case ExpressionKind::null_literal:
      return semantic(semantics_, expression).inferred_type = ValueType::null_value;
    case ExpressionKind::omitted_argument:
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    case ExpressionKind::end_index:
      if (semantic::requires_runtime_extent(semantic(semantics_, expression).index_extent)) {
        return semantic(semantics_, expression).inferred_type = ValueType::integer;
      }
      diagnose(expression.location.line, "MPF2048",
               "Matlab 'end' is only valid in an array index selector");
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    case ExpressionKind::identifier: {
      const auto* use = names_.reference(expression.id);
      if (use != nullptr && use->symbol.valid()) {
        auto* symbol = lookup(use->symbol);
        semantic(semantics_, expression).binding = symbol->binding;
        semantic(semantics_, expression).inferred_type = symbol->type;
        semantic(semantics_, expression).element_type = symbol->element_type;
        semantic(semantics_, expression).shape = symbol->shape;
        semantic(semantics_, expression).tuple_types = symbol->tuple_types;
        semantic(semantics_, expression).tuple_element_types = symbol->tuple_element_types;
        semantic(semantics_, expression).tuple_shapes = symbol->tuple_shapes;
        semantic(semantics_, expression).sequence_is_list = symbol->sequence_is_list;
        semantic(semantics_, expression).sequence_elements = symbol->sequence_elements;
        if (symbol->binding == BindingKind::variable) {
          semantic(semantics_, expression).storage_region = full_storage_region(symbol->shape);
        }
        if (symbol->binding == BindingKind::variable) {
          mark_fortran_parameter_read(use->symbol);
        }
        if (symbol->binding == BindingKind::variable && !symbol->assigned &&
            (scopes_.size() == 1 || belongs_to_current_scope(use->symbol))) {
          diagnose(expression.location.line, "MPF2003",
                   "variable '" + expression.value + "' is used before it is definitely assigned");
        }
        return semantic(semantics_, expression).inferred_type;
      }
      const auto intrinsic = use == nullptr ? IntrinsicId::none : use->intrinsic;
      if (intrinsic != IntrinsicId::none) {
        semantic(semantics_, expression).binding = BindingKind::builtin;
        semantic(semantics_, expression).intrinsic = intrinsic;
        semantic(semantics_, expression).inferred_type =
            intrinsic == IntrinsicId::not_a_number || intrinsic == IntrinsicId::infinity
                ? ValueType::real
                : ValueType::function;
        return semantic(semantics_, expression).inferred_type;
      }
      diagnose(expression.location.line, "MPF2001",
               "undefined identifier '" + expression.value + "'");
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    }
    case ExpressionKind::unary: {
      const auto operand = expression.children.empty()
                               ? ValueType::unknown
                               : analyze_expression(expression.children.front());
      if (expression.unary_operation == UnaryOperator::transpose ||
          expression.unary_operation == UnaryOperator::conjugate_transpose) {
        auto& facts = semantic(semantics_, expression);
        const auto& operand_facts = semantic(semantics_, expression.children.front());
        if (operand == ValueType::list) {
          if (operand_facts.shape.size() == 1U) {
            facts.shape = {operand_facts.shape.front(), 1U};
          } else if (operand_facts.shape.size() == 2U) {
            facts.shape = {operand_facts.shape[1], operand_facts.shape[0]};
          } else {
            diagnose(expression.location.line, "MPF2047",
                     "Matlab transpose requires a vector or rank-2 array; use page-wise "
                     "operations for N-D arrays");
            return facts.inferred_type = ValueType::unknown;
          }
          facts.inferred_type = ValueType::list;
          facts.element_type = operand_facts.element_type;
          return facts.inferred_type;
        }
        if (operand == ValueType::string) {
          diagnose(expression.location.line, "MPF2047",
                   "Matlab character-array transpose is outside the current string model");
          return facts.inferred_type = ValueType::unknown;
        }
        return facts.inferred_type = operand;
      }
      if (program_.language == SourceLanguage::typescript && expression.value == "!" &&
          operand != ValueType::unknown && operand != ValueType::boolean) {
        diagnose(expression.location.line, "MPF2002",
                 "TypeScript logical negation currently requires a boolean operand");
      }
      semantic(semantics_, expression).inferred_type =
          expression.value == "!" ? ValueType::boolean : operand;
      return semantic(semantics_, expression).inferred_type;
    }
    case ExpressionKind::binary: return analyze_binary(expression);
    case ExpressionKind::comparison_chain: return analyze_comparison_chain(expression);
    case ExpressionKind::conditional: return analyze_conditional(expression);
    case ExpressionKind::call: return analyze_call(expression);
    case ExpressionKind::member:
      if (!expression.children.empty()) analyze_expression(expression.children.front());
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    case ExpressionKind::index: return analyze_index(expression);
    case ExpressionKind::slice:
      diagnose(expression.location.line, "MPF2029",
               "slice/colon expressions are only valid as array/list subscripts");
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    case ExpressionKind::list: {
      ValueType element_type = ValueType::unknown;
      bool incompatible = false;
      bool ragged = false;
      bool nested = !expression.children.empty();
      std::vector<std::size_t> nested_shape;
      for (auto& child : expression.children) {
        const auto child_type = analyze_expression(child);
        const auto& child_facts = semantic(semantics_, child);
        nested = nested && child_type == ValueType::list;
        if (child_type == ValueType::list) {
          if (nested_shape.empty())
            nested_shape = child_facts.shape;
          else if (nested_shape != child_facts.shape) {
            incompatible = true;
            ragged = true;
          }
        }
        const auto scalar_type =
            child_type == ValueType::list ? child_facts.element_type : child_type;
        const auto joined = join_types(element_type, scalar_type);
        if (element_type != ValueType::unknown && child_type != ValueType::unknown &&
            joined == ValueType::unknown)
          incompatible = true;
        element_type = joined;
      }
      const bool mixed_nesting =
          std::any_of(expression.children.begin(), expression.children.end(),
                      [&](const Expression& child) {
                        return semantic(semantics_, child).inferred_type == ValueType::list;
                      }) &&
          !nested;
      incompatible = incompatible || mixed_nesting;
      if (incompatible && program_.language != SourceLanguage::python) {
        diagnose(expression.location.line, "MPF2020",
                 "target array/list requires a homogeneous element type");
      }
      auto& facts = semantic(semantics_, expression);
      // Matlab defines [] as a 0-by-0 double array.  Keep that source-language
      // contract in the semantic side table instead of collapsing it to a
      // language-neutral rank-one empty container.  Other frontends retain
      // their native empty-list model.
      if (program_.language == SourceLanguage::matlab && expression.children.empty()) {
        element_type = ValueType::real;
        facts.shape = {0U, 0U};
      } else {
        facts.shape = {expression.children.size()};
      }
      facts.element_type = element_type;
      facts.column_major = program_.semantics.layout == semantic::IndexLayout::column_major;
      if (nested && !nested_shape.empty() && !ragged) {
        facts.shape.insert(facts.shape.end(), nested_shape.begin(), nested_shape.end());
      } else if (nested && ragged) {
        const auto maximum =
            std::max_element(expression.children.begin(), expression.children.end(),
                             [&](const Expression& left, const Expression& right) {
                               return semantic(semantics_, left).shape.size() <
                                      semantic(semantics_, right).shape.size();
                             });
        const auto maximum_rank = semantic(semantics_, *maximum).shape.size();
        facts.shape.insert(facts.shape.end(), maximum_rank, dynamic_extent);
      } else if (mixed_nesting) {
        facts.element_type = ValueType::unknown;
      }
      facts.sequence_is_list = true;
      facts.sequence_elements.clear();
      facts.sequence_elements.reserve(expression.children.size());
      for (const auto& child : expression.children) {
        facts.sequence_elements.push_back(expression_metadata(child, semantics_));
      }
      return facts.inferred_type = ValueType::list;
    }
    case ExpressionKind::tuple:
      semantic(semantics_, expression).tuple_types.clear();
      semantic(semantics_, expression).tuple_element_types.clear();
      semantic(semantics_, expression).tuple_shapes.clear();
      for (auto& child : expression.children) {
        semantic(semantics_, expression).tuple_types.push_back(analyze_expression(child));
        semantic(semantics_, expression)
            .tuple_element_types.push_back(semantic(semantics_, child).element_type);
        semantic(semantics_, expression).tuple_shapes.push_back(semantic(semantics_, child).shape);
      }
      semantic(semantics_, expression).sequence_is_list = false;
      semantic(semantics_, expression).sequence_elements.clear();
      semantic(semantics_, expression).sequence_elements.reserve(expression.children.size());
      for (const auto& child : expression.children) {
        semantic(semantics_, expression)
            .sequence_elements.push_back(expression_metadata(child, semantics_));
      }
      return semantic(semantics_, expression).inferred_type = ValueType::tuple;
  }
  return ValueType::unknown;
}

ValueType Analyzer::analyze_binary(Expression& expression) {
  const auto left = analyze_expression(expression.children[0]);
  const auto right = analyze_expression(expression.children[1]);
  const auto operation = expression.operation;
  if (expression.comparison != ComparisonOperator::none) {
    if (program_.language == SourceLanguage::matlab) {
      const auto& left_facts = semantic(semantics_, expression.children[0]);
      const auto& right_facts = semantic(semantics_, expression.children[1]);
      const bool left_array = left == ValueType::list;
      const bool right_array = right == ValueType::list;
      const bool runtime_operand = left == ValueType::unknown || right == ValueType::unknown;
      const auto numeric_logical_or_unknown = [](const ValueType type) {
        return type == ValueType::unknown || numeric(type) || type == ValueType::boolean;
      };
      const bool runtime_array_comparison =
          runtime_operand && numeric_logical_or_unknown(left) && numeric_logical_or_unknown(right);
      if (left_array || right_array || runtime_array_comparison) {
        const auto left_element = left_array ? left_facts.element_type : left;
        const auto right_element = right_array ? right_facts.element_type : right;
        if ((left_element != ValueType::unknown && !numeric(left_element) &&
             left_element != ValueType::boolean) ||
            (right_element != ValueType::unknown && !numeric(right_element) &&
             right_element != ValueType::boolean)) {
          diagnose(expression.location.line, "MPF2046",
                   "Matlab array comparison requires numeric or logical operands");
          return semantic(semantics_, expression).inferred_type = ValueType::unknown;
        }
        auto& facts = semantic(semantics_, expression);
        facts.array_operation = semantic::ArrayOperation::matlab;
        if (runtime_operand) {
          facts.broadcast = matlab_runtime_broadcast_plan();
        } else {
          const auto broadcast =
              matlab_broadcast_plan(left_array ? left_facts.shape : std::vector<std::size_t>{},
                                    right_array ? right_facts.shape : std::vector<std::size_t>{});
          if (!broadcast.has_value()) {
            diagnose(expression.location.line, "MPF2046",
                     "Matlab array comparison operands have incompatible sizes");
            return facts.inferred_type = ValueType::unknown;
          }
          if (!left_array || !right_array || left_facts.shape != right_facts.shape ||
              broadcast->shape_source == semantic::BroadcastShapeSource::runtime_operands) {
            facts.broadcast = *broadcast;
          }
        }
        facts.inferred_type = left_array || right_array ? ValueType::list : ValueType::unknown;
        facts.element_type = ValueType::boolean;
        facts.shape = facts.broadcast.valid ? facts.broadcast.result_shape
                                            : (left_array ? left_facts.shape : right_facts.shape);
        return facts.inferred_type;
      }
    }
    if (program_.language == SourceLanguage::python) {
      validate_python_comparison(expression.comparison, expression.children[0], left,
                                 expression.children[1], right);
    } else if (program_.language == SourceLanguage::typescript && left != ValueType::unknown &&
               right != ValueType::unknown) {
      const auto numeric_type = [](const ValueType type) {
        return type == ValueType::integer || type == ValueType::real;
      };
      const bool compatible = (numeric_type(left) && numeric_type(right)) || left == right;
      const bool reference_value = left == ValueType::list || left == ValueType::tuple ||
                                   right == ValueType::list || right == ValueType::tuple;
      if (!compatible || reference_value ||
          (comparison_is_ordering(expression.comparison) && left == ValueType::boolean)) {
        diagnose(expression.location.line, "MPF2044",
                 "TypeScript comparison operands are outside the portable scalar subset");
      }
    }
    return semantic(semantics_, expression).inferred_type = ValueType::boolean;
  }
  if (program_.semantics.logical_result == semantic::LogicalResult::operand &&
      (operation == BinaryOperator::logical_and || operation == BinaryOperator::logical_or)) {
    semantic(semantics_, expression).inferred_type = join_types(left, right);
    if (left == ValueType::list && right == ValueType::list) {
      const auto& left_facts = semantic(semantics_, expression.children[0]);
      const auto& right_facts = semantic(semantics_, expression.children[1]);
      semantic(semantics_, expression).element_type =
          join_types(left_facts.element_type, right_facts.element_type);
      if (left_facts.shape == right_facts.shape) {
        semantic(semantics_, expression).shape = left_facts.shape;
      } else {
        const auto rank = std::max(left_facts.shape.size(), right_facts.shape.size());
        semantic(semantics_, expression).shape.assign(rank, dynamic_extent);
      }
    }
    return semantic(semantics_, expression).inferred_type;
  }
  if (operation == BinaryOperator::logical_and || operation == BinaryOperator::logical_or) {
    if (program_.language == SourceLanguage::typescript &&
        ((left != ValueType::boolean && left != ValueType::unknown) ||
         (right != ValueType::boolean && right != ValueType::unknown))) {
      diagnose(expression.location.line, "MPF2002",
               "TypeScript logical operators currently require boolean operands");
    }
    return semantic(semantics_, expression).inferred_type = ValueType::boolean;
  }
  if (operation == BinaryOperator::add && left == ValueType::string && right == ValueType::string) {
    return semantic(semantics_, expression).inferred_type = ValueType::string;
  }
  if (program_.language == SourceLanguage::matlab) {
    const auto& left_facts = semantic(semantics_, expression.children[0]);
    const auto& right_facts = semantic(semantics_, expression.children[1]);
    const bool left_array = left == ValueType::list;
    const bool right_array = right == ValueType::list;
    const bool has_array = left_array || right_array;
    const bool runtime_operand = left == ValueType::unknown || right == ValueType::unknown;
    const auto left_element = left_array ? left_facts.element_type : left;
    const auto right_element = right_array ? right_facts.element_type : right;
    const auto numeric_or_unknown = [](const ValueType type) {
      return numeric(type) || type == ValueType::unknown;
    };
    const bool elementwise = operation == BinaryOperator::add ||
                             operation == BinaryOperator::subtract ||
                             operation == BinaryOperator::elementwise_multiply ||
                             operation == BinaryOperator::elementwise_divide ||
                             operation == BinaryOperator::elementwise_left_divide ||
                             operation == BinaryOperator::elementwise_power;
    const bool scalar_scale = operation == BinaryOperator::multiply && left_array != right_array;
    const bool scalar_matrix_division =
        (operation == BinaryOperator::divide && left_array && !right_array) ||
        (operation == BinaryOperator::left_divide && !left_array && right_array);

    if ((elementwise || scalar_scale || scalar_matrix_division) &&
        (!numeric_or_unknown(left_element) || !numeric_or_unknown(right_element))) {
      diagnose(expression.location.line, "MPF2046",
               "Matlab array operator '" + expression.value +
                   "' requires numeric scalar or array operands");
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    }
    if ((has_array || (runtime_operand && elementwise)) &&
        (elementwise || scalar_scale || scalar_matrix_division)) {
      auto& facts = semantic(semantics_, expression);
      facts.array_operation = semantic::ArrayOperation::matlab;
      if (runtime_operand) {
        facts.broadcast = matlab_runtime_broadcast_plan();
      } else {
        const auto broadcast =
            matlab_broadcast_plan(left_array ? left_facts.shape : std::vector<std::size_t>{},
                                  right_array ? right_facts.shape : std::vector<std::size_t>{});
        if (!broadcast.has_value()) {
          diagnose(expression.location.line, "MPF2046",
                   "Matlab element-wise operands have incompatible sizes; every dimension must "
                   "be equal or singleton");
          return facts.inferred_type = ValueType::unknown;
        }
        // Preserve the direct same-shape runtime fast path. A side-table plan is needed only
        // when lowering must expand at least one compatible singleton dimension.
        if (!left_array || !right_array || left_facts.shape != right_facts.shape ||
            broadcast->shape_source == semantic::BroadcastShapeSource::runtime_operands) {
          facts.broadcast = *broadcast;
        }
      }
      facts.inferred_type = has_array ? ValueType::list : ValueType::unknown;
      facts.element_type = operation == BinaryOperator::divide ||
                                   operation == BinaryOperator::left_divide ||
                                   operation == BinaryOperator::elementwise_divide ||
                                   operation == BinaryOperator::elementwise_left_divide ||
                                   operation == BinaryOperator::elementwise_power
                               ? ValueType::real
                               : join_types(left_element, right_element);
      facts.shape = facts.broadcast.valid ? facts.broadcast.result_shape
                                          : (left_array ? left_facts.shape : right_facts.shape);
      return facts.inferred_type;
    }
    if (has_array && operation == BinaryOperator::multiply && left_array && right_array) {
      if (left_facts.shape.size() != 2U || right_facts.shape.size() != 2U) {
        diagnose(expression.location.line, "MPF2046",
                 "Matlab matrix multiplication currently requires rank-2 operands");
        return semantic(semantics_, expression).inferred_type = ValueType::unknown;
      }
      const auto left_inner = left_facts.shape[1];
      const auto right_inner = right_facts.shape[0];
      if (left_inner != dynamic_extent && right_inner != dynamic_extent &&
          left_inner != right_inner) {
        diagnose(expression.location.line, "MPF2046",
                 "Matlab matrix multiplication operands have incompatible inner extents");
        return semantic(semantics_, expression).inferred_type = ValueType::unknown;
      }
      if (!numeric_or_unknown(left_element) || !numeric_or_unknown(right_element)) {
        diagnose(expression.location.line, "MPF2046",
                 "Matlab matrix multiplication requires numeric array operands");
        return semantic(semantics_, expression).inferred_type = ValueType::unknown;
      }
      auto& facts = semantic(semantics_, expression);
      facts.array_operation = semantic::ArrayOperation::matlab;
      facts.inferred_type = ValueType::list;
      facts.element_type = join_types(left_element, right_element);
      facts.shape = {left_facts.shape[0], right_facts.shape[1]};
      if (!static_rank_two_shape(left_facts.shape) || !static_rank_two_shape(right_facts.shape)) {
        diagnose(expression.location.line, "MPF2046",
                 "Matlab matrix multiplication currently requires static rank-2 extents");
        return facts.inferred_type = ValueType::unknown;
      }
      facts.matrix_operation = {semantic::MatrixOperation::multiply,
                                semantic::MatrixSolveKind::none,
                                semantic::MatrixRankPolicy::none,
                                left_facts.shape,
                                right_facts.shape,
                                facts.shape};
      return facts.inferred_type;
    }
    if (left_array && right_array &&
        (operation == BinaryOperator::left_divide || operation == BinaryOperator::divide)) {
      if (!numeric_or_unknown(left_element) || !numeric_or_unknown(right_element)) {
        diagnose(expression.location.line, "MPF2046",
                 "Matlab matrix division requires numeric array operands");
        return semantic(semantics_, expression).inferred_type = ValueType::unknown;
      }
      const auto left_matrix_shape = static_matlab_matrix_shape(left_facts.shape);
      const auto right_matrix_shape = static_matlab_matrix_shape(right_facts.shape);
      if (!left_matrix_shape.has_value() || !right_matrix_shape.has_value()) {
        diagnose(expression.location.line, "MPF2046",
                 "Matlab matrix division currently requires static rank-2 operands");
        return semantic(semantics_, expression).inferred_type = ValueType::unknown;
      }
      auto& facts = semantic(semantics_, expression);
      facts.array_operation = semantic::ArrayOperation::matlab;
      facts.inferred_type = ValueType::list;
      facts.element_type = ValueType::real;
      if (operation == BinaryOperator::left_divide) {
        if ((*left_matrix_shape)[0] != (*right_matrix_shape)[0]) {
          diagnose(expression.location.line, "MPF2046",
                   "Matlab matrix left division requires a right-hand side with the same row "
                   "extent as its coefficient matrix");
          return facts.inferred_type = ValueType::unknown;
        }
        facts.shape = {(*left_matrix_shape)[1], (*right_matrix_shape)[1]};
        const auto solve =
            semantic::matrix_solve_kind((*left_matrix_shape)[0], (*left_matrix_shape)[1]);
        facts.matrix_operation = {semantic::MatrixOperation::left_divide,
                                  solve,
                                  semantic::matrix_rank_policy(solve),
                                  *left_matrix_shape,
                                  *right_matrix_shape,
                                  facts.shape};
      } else {
        if ((*left_matrix_shape)[1] != (*right_matrix_shape)[1]) {
          diagnose(expression.location.line, "MPF2046",
                   "Matlab matrix right division requires both operands to have the same column "
                   "extent");
          return facts.inferred_type = ValueType::unknown;
        }
        facts.shape = {(*left_matrix_shape)[0], (*right_matrix_shape)[0]};
        const auto solve =
            semantic::matrix_solve_kind((*right_matrix_shape)[1], (*right_matrix_shape)[0]);
        facts.matrix_operation = {semantic::MatrixOperation::right_divide,
                                  solve,
                                  semantic::matrix_rank_policy(solve),
                                  *left_matrix_shape,
                                  *right_matrix_shape,
                                  facts.shape};
      }
      return facts.inferred_type;
    }
    if (left_array && !right_array && operation == BinaryOperator::power) {
      if (!numeric_or_unknown(left_element) || !numeric_or_unknown(right)) {
        diagnose(expression.location.line, "MPF2046",
                 "Matlab matrix power requires a numeric matrix and numeric scalar exponent");
        return semantic(semantics_, expression).inferred_type = ValueType::unknown;
      }
      if (!static_rank_two_shape(left_facts.shape) || left_facts.shape[0] != left_facts.shape[1]) {
        diagnose(expression.location.line, "MPF2046",
                 "Matlab matrix power currently requires a static square rank-2 matrix");
        return semantic(semantics_, expression).inferred_type = ValueType::unknown;
      }
      const auto exponent_constant = numeric_constant(expression.children[1]);
      const auto integral_exponent = integral_constant(expression.children[1]);
      constexpr long long maximum_safe_integer = 9007199254740991LL;
      if (exponent_constant.has_value() &&
          (!integral_exponent.has_value() || *integral_exponent < -maximum_safe_integer ||
           *integral_exponent > maximum_safe_integer)) {
        diagnose(expression.location.line, "MPF2046",
                 "Matlab matrix power currently requires an ECMAScript-safe integer exponent");
        return semantic(semantics_, expression).inferred_type = ValueType::unknown;
      }
      auto& facts = semantic(semantics_, expression);
      facts.array_operation = semantic::ArrayOperation::matlab;
      facts.inferred_type = ValueType::list;
      facts.element_type = ValueType::real;
      facts.shape = left_facts.shape;
      facts.matrix_operation = {semantic::MatrixOperation::integer_power,
                                semantic::MatrixSolveKind::none,
                                semantic::MatrixRankPolicy::none,
                                left_facts.shape,
                                {},
                                facts.shape};
      return facts.inferred_type;
    }
    if (has_array &&
        (operation == BinaryOperator::divide || operation == BinaryOperator::left_divide ||
         operation == BinaryOperator::power)) {
      diagnose(expression.location.line, "MPF2046",
               "Matlab matrix operator '" + expression.value +
                   "' is not yet supported for array operands");
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    }
    if (!has_array &&
        (operation == BinaryOperator::elementwise_multiply ||
         operation == BinaryOperator::elementwise_divide ||
         operation == BinaryOperator::elementwise_left_divide ||
         operation == BinaryOperator::elementwise_power ||
         operation == BinaryOperator::left_divide || operation == BinaryOperator::power)) {
      if ((!numeric(left) || !numeric(right)) && left != ValueType::unknown &&
          right != ValueType::unknown) {
        diagnose(expression.location.line, "MPF2002",
                 "operator '" + expression.value + "' cannot be applied to " + to_string(left) +
                     " and " + to_string(right));
        return semantic(semantics_, expression).inferred_type = ValueType::unknown;
      }
      if (operation == BinaryOperator::elementwise_multiply) {
        return semantic(semantics_, expression).inferred_type = join_types(left, right);
      }
      return semantic(semantics_, expression).inferred_type = ValueType::real;
    }
  }
  if ((!numeric(left) || !numeric(right)) && left != ValueType::unknown &&
      right != ValueType::unknown) {
    diagnose(expression.location.line, "MPF2002",
             "operator '" + expression.value + "' cannot be applied to " + to_string(left) +
                 " and " + to_string(right));
    return semantic(semantics_, expression).inferred_type = ValueType::unknown;
  }
  if (operation == BinaryOperator::floor_divide)
    return semantic(semantics_, expression).inferred_type = ValueType::integer;
  if (operation == BinaryOperator::divide &&
      program_.semantics.division == semantic::Division::real_quotient) {
    return semantic(semantics_, expression).inferred_type = ValueType::real;
  }
  if (operation == BinaryOperator::power)
    return semantic(semantics_, expression).inferred_type = ValueType::real;
  return semantic(semantics_, expression).inferred_type = join_types(left, right);
}

void Analyzer::validate_python_comparison(const ComparisonOperator operation,
                                          const Expression& left, const ValueType left_type,
                                          const Expression& right, const ValueType right_type) {
  if (comparison_is_ordering(operation)) {
    validate_python_ordering(left_type, right_type, left.location.line);
    return;
  }
  if (comparison_is_identity(operation)) {
    if (left_type == ValueType::unknown || right_type == ValueType::unknown ||
        left_type == ValueType::null_value || right_type == ValueType::null_value ||
        left_type == ValueType::boolean || right_type == ValueType::boolean ||
        ((left_type == ValueType::list || left_type == ValueType::tuple) &&
         (right_type == ValueType::list || right_type == ValueType::tuple))) {
      return;
    }
    diagnose(left.location.line, "MPF2045",
             "Python identity currently supports singleton and sequence object operands; "
             "numeric and string object interning is not portable");
    return;
  }
  if (!comparison_is_membership(operation) || right_type == ValueType::unknown) return;
  if (right_type == ValueType::string) {
    if (left_type != ValueType::unknown && left_type != ValueType::string) {
      diagnose(right.location.line, "MPF2045",
               "Python string membership requires a string left operand");
    }
    return;
  }
  if (right_type != ValueType::list && right_type != ValueType::tuple) {
    diagnose(right.location.line, "MPF2045",
             "Python membership currently requires a string, list, or tuple container");
  }
}

void Analyzer::validate_python_ordering(const ValueType left, const ValueType right,
                                        const std::size_t line) {
  if (left == ValueType::unknown || right == ValueType::unknown) return;
  const auto numeric_like = [](const ValueType type) {
    return type == ValueType::integer || type == ValueType::real || type == ValueType::boolean;
  };
  if ((numeric_like(left) && numeric_like(right)) ||
      (left == ValueType::string && right == ValueType::string) ||
      (left == ValueType::list && right == ValueType::list) ||
      (left == ValueType::tuple && right == ValueType::tuple)) {
    return;
  }
  diagnose(line, "MPF2044", "Python ordering comparison has incompatible operand types");
}

ValueType Analyzer::analyze_comparison_chain(Expression& expression) {
  if (expression.children.size() < 3 ||
      expression.comparisons.size() + 1 != expression.children.size()) {
    diagnose(expression.location.line, "MPF2044", "malformed Python comparison chain IR");
    return semantic(semantics_, expression).inferred_type = ValueType::unknown;
  }
  std::vector<ValueType> operand_types;
  operand_types.reserve(expression.children.size());
  for (auto& child : expression.children) {
    operand_types.push_back(analyze_expression(child));
  }
  for (std::size_t index = 0; index < expression.comparisons.size(); ++index) {
    validate_python_comparison(expression.comparisons[index], expression.children[index],
                               operand_types[index], expression.children[index + 1],
                               operand_types[index + 1]);
  }
  return semantic(semantics_, expression).inferred_type = ValueType::boolean;
}

ValueType Analyzer::analyze_conditional(Expression& expression) {
  if (expression.children.size() != 3) {
    diagnose(expression.location.line, "MPF2044", "malformed Python conditional expression IR");
    return semantic(semantics_, expression).inferred_type = ValueType::unknown;
  }
  analyze_expression(expression.children[0]);
  const auto true_type = analyze_expression(expression.children[1]);
  const auto false_type = analyze_expression(expression.children[2]);
  semantic(semantics_, expression).inferred_type = join_types(true_type, false_type);
  const auto& true_facts = semantic(semantics_, expression.children[1]);
  const auto& false_facts = semantic(semantics_, expression.children[2]);
  if (true_type == ValueType::list && false_type == ValueType::list) {
    semantic(semantics_, expression).element_type =
        join_types(true_facts.element_type, false_facts.element_type);
    if (true_facts.shape == false_facts.shape) {
      semantic(semantics_, expression).shape = true_facts.shape;
    } else {
      const auto rank = std::max(true_facts.shape.size(), false_facts.shape.size());
      semantic(semantics_, expression).shape.assign(rank, dynamic_extent);
    }
  }
  if (true_type == ValueType::tuple && false_type == ValueType::tuple &&
      true_facts.tuple_types == false_facts.tuple_types &&
      true_facts.tuple_element_types == false_facts.tuple_element_types &&
      true_facts.tuple_shapes == false_facts.tuple_shapes) {
    semantic(semantics_, expression).tuple_types = true_facts.tuple_types;
    semantic(semantics_, expression).tuple_element_types = true_facts.tuple_element_types;
    semantic(semantics_, expression).tuple_shapes = true_facts.tuple_shapes;
  }
  if ((true_type == ValueType::list || true_type == ValueType::tuple) && true_type == false_type &&
      true_facts.sequence_is_list == false_facts.sequence_is_list &&
      same_metadata(true_facts.sequence_elements, false_facts.sequence_elements)) {
    semantic(semantics_, expression).sequence_is_list = true_facts.sequence_is_list;
    semantic(semantics_, expression).sequence_elements = true_facts.sequence_elements;
  }
  return semantic(semantics_, expression).inferred_type;
}

void Analyzer::normalize_fortran_arguments(Expression& expression, const Statement& function) {
  std::vector<HirNodeId> original_children;
  original_children.reserve(expression.children.size());
  for (const auto& child : expression.children) original_children.push_back(child.id);
  const auto actual_count = expression.children.size() - 1;
  if (semantic(semantics_, expression).argument_names.size() < actual_count) {
    semantic(semantics_, expression).argument_names.resize(actual_count);
  }
  std::vector<std::optional<Expression>> associated(function.parameters.size());
  std::vector<bool> used(function.parameters.size(), false);
  std::size_t positional = 0;
  bool saw_keyword = false;
  for (std::size_t index = 0; index < actual_count; ++index) {
    auto actual = std::move(expression.children[index + 1]);
    const auto& keyword = semantic(semantics_, expression).argument_names[index];
    std::size_t formal;
    if (keyword.empty()) {
      if (saw_keyword) {
        diagnose(actual.location.line, "MPF2040",
                 "Fortran positional actual argument cannot follow a keyword argument");
      }
      while (positional < used.size() && used[positional]) ++positional;
      formal = positional++;
      if (formal >= function.parameters.size()) {
        diagnose(actual.location.line, "MPF2034",
                 "procedure '" + function.name + "' received too many actual arguments");
        continue;
      }
    } else {
      saw_keyword = true;
      const auto found = std::find(function.parameters.begin(), function.parameters.end(), keyword);
      if (found == function.parameters.end()) {
        diagnose(actual.location.line, "MPF2040",
                 "unknown keyword actual argument '" + keyword + "' for procedure '" +
                     function.name + "'");
        continue;
      }
      formal = static_cast<std::size_t>(std::distance(function.parameters.begin(), found));
    }
    if (used[formal]) {
      diagnose(actual.location.line, "MPF2040",
               "dummy argument '" + function.parameters[formal] + "' is associated more than once");
      continue;
    }
    used[formal] = true;
    associated[formal] = std::move(actual);
  }

  std::vector<Expression> normalized;
  normalized.reserve(function.parameters.size() + 1);
  normalized.push_back(std::move(expression.children.front()));
  for (std::size_t index = 0; index < function.parameters.size(); ++index) {
    if (associated[index].has_value()) {
      normalized.push_back(std::move(*associated[index]));
      continue;
    }
    const bool optional = index < semantic(semantics_, function).parameter_optional.size() &&
                          semantic(semantics_, function).parameter_optional[index];
    if (!optional) {
      diagnose(expression.location.line, "MPF2034",
               "required dummy argument '" + function.parameters[index] +
                   "' is not associated in call to '" + function.name + "'");
    }
    Expression omitted;
    omitted.kind = ExpressionKind::omitted_argument;
    omitted.location = expression.location;
    register_expression(omitted);
    normalized.push_back(std::move(omitted));
  }
  expression.children = std::move(normalized);
  structure_changed_ =
      structure_changed_ || expression.children.size() != original_children.size() ||
      !std::equal(expression.children.begin(), expression.children.end(), original_children.begin(),
                  original_children.end(),
                  [](const Expression& child, const HirNodeId id) { return child.id == id; });
  semantic(semantics_, expression).argument_names.assign(function.parameters.size(), {});
  semantic(semantics_, expression)
      .argument_optional_forward.assign(function.parameters.size(), false);
}

void Analyzer::normalize_python_arguments(Expression& expression, const Statement& function) {
  std::vector<HirNodeId> original_children;
  original_children.reserve(expression.children.size());
  for (const auto& child : expression.children) original_children.push_back(child.id);
  const auto actual_count = expression.children.size() - 1;
  if (semantic(semantics_, expression).argument_names.size() < actual_count) {
    semantic(semantics_, expression).argument_names.resize(actual_count);
  }
  std::vector<std::optional<Expression>> associated(function.parameters.size());
  std::vector<bool> used(function.parameters.size(), false);
  std::size_t positional = 0;
  bool saw_keyword = false;
  for (std::size_t index = 0; index < actual_count; ++index) {
    auto actual = std::move(expression.children[index + 1]);
    const auto& keyword = semantic(semantics_, expression).argument_names[index];
    std::size_t formal;
    if (keyword.empty()) {
      if (saw_keyword) {
        diagnose(actual.location.line, "MPF2041",
                 "Python positional argument cannot follow a keyword argument");
      }
      while (positional < function.parameters.size()) {
        const auto kind = positional < function.parameter_kinds.size()
                              ? function.parameter_kinds[positional]
                              : ParameterKind::positional_or_keyword;
        if (!used[positional] && kind != ParameterKind::keyword_only) break;
        ++positional;
      }
      formal = positional++;
      if (formal >= function.parameters.size()) {
        diagnose(actual.location.line, "MPF2034",
                 "function '" + function.name + "' received too many positional arguments");
        continue;
      }
    } else {
      saw_keyword = true;
      const auto found = std::find(function.parameters.begin(), function.parameters.end(), keyword);
      if (found == function.parameters.end()) {
        diagnose(actual.location.line, "MPF2041",
                 "unknown keyword argument '" + keyword + "' for function '" + function.name + "'");
        continue;
      }
      formal = static_cast<std::size_t>(std::distance(function.parameters.begin(), found));
      const auto kind = formal < function.parameter_kinds.size()
                            ? function.parameter_kinds[formal]
                            : ParameterKind::positional_or_keyword;
      if (kind == ParameterKind::positional_only) {
        diagnose(actual.location.line, "MPF2041",
                 "positional-only parameter '" + keyword + "' cannot be passed by keyword");
        continue;
      }
    }
    if (used[formal]) {
      diagnose(actual.location.line, "MPF2041",
               "parameter '" + function.parameters[formal] + "' receives more than one argument");
      continue;
    }
    used[formal] = true;
    associated[formal] = std::move(actual);
  }

  std::vector<Expression> normalized;
  normalized.reserve(function.parameters.size() + 1);
  normalized.push_back(std::move(expression.children.front()));
  for (std::size_t index = 0; index < function.parameters.size(); ++index) {
    if (associated[index].has_value()) {
      normalized.push_back(std::move(*associated[index]));
    } else if (index < function.parameter_defaults.size() &&
               function.parameter_defaults[index].valid()) {
      normalized.push_back(clone_expression(function.parameter_defaults[index]));
    } else {
      diagnose(expression.location.line, "MPF2034",
               "required parameter '" + function.parameters[index] + "' is missing in call to '" +
                   function.name + "'");
      Expression omitted;
      omitted.kind = ExpressionKind::omitted_argument;
      omitted.location = expression.location;
      register_expression(omitted);
      normalized.push_back(std::move(omitted));
    }
  }
  expression.children = std::move(normalized);
  structure_changed_ =
      structure_changed_ || expression.children.size() != original_children.size() ||
      !std::equal(expression.children.begin(), expression.children.end(), original_children.begin(),
                  original_children.end(),
                  [](const Expression& child, const HirNodeId id) { return child.id == id; });
  semantic(semantics_, expression).argument_names.assign(function.parameters.size(), {});
}

ValueType Analyzer::analyze_call(Expression& expression) {
  if (expression.children.empty())
    return semantic(semantics_, expression).inferred_type = ValueType::unknown;
  auto& callee = expression.children.front();
  analyze_expression(callee);
  auto& callee_facts = semantic(semantics_, callee);
  if ((program_.language == SourceLanguage::matlab ||
       program_.language == SourceLanguage::fortran) &&
      callee.kind == ExpressionKind::identifier && callee_facts.binding == BindingKind::variable &&
      (callee_facts.inferred_type == ValueType::list ||
       callee_facts.inferred_type == ValueType::unknown) &&
      expression.children.size() >= 2) {
    if (program_.language == SourceLanguage::fortran &&
        std::any_of(semantic(semantics_, expression).argument_names.begin(),
                    semantic(semantics_, expression).argument_names.end(),
                    [](const std::string& name) { return !name.empty(); })) {
      diagnose(expression.location.line, "MPF2040",
               "Fortran array subscripts cannot use procedure argument keywords");
    }
    expression.kind = ExpressionKind::index;
    semantic(semantics_, expression).index_base = 1;
    semantic(semantics_, expression).allow_negative_index = false;
    semantic(semantics_, expression).column_major = true;
    return analyze_index(expression, true);
  }
  const Statement* called_function = nullptr;
  if (callee_facts.binding == BindingKind::function && callee.kind == ExpressionKind::identifier) {
    const auto* use = names_.reference(callee.id);
    const auto* symbol = use == nullptr ? nullptr : names_.symbol(use->symbol);
    called_function =
        symbol == nullptr ? nullptr : find_statement(program_.statements, symbol->declaration);
    if (called_function != nullptr && called_function->kind != StatementKind::function) {
      called_function = nullptr;
    }
  }
  if (program_.language == SourceLanguage::fortran && called_function != nullptr) {
    normalize_fortran_arguments(expression, *called_function);
  } else if (program_.semantics.emit_parameter_defaults && called_function != nullptr) {
    normalize_python_arguments(expression, *called_function);
  } else if (program_.language == SourceLanguage::fortran &&
             std::any_of(semantic(semantics_, expression).argument_names.begin(),
                         semantic(semantics_, expression).argument_names.end(),
                         [](const std::string& name) { return !name.empty(); })) {
    diagnose(expression.location.line, "MPF2040",
             "Fortran keyword actual arguments require a known procedure interface");
  } else if (program_.language == SourceLanguage::python &&
             std::any_of(semantic(semantics_, expression).argument_names.begin(),
                         semantic(semantics_, expression).argument_names.end(),
                         [](const std::string& name) { return !name.empty(); })) {
    diagnose(expression.location.line, "MPF2041",
             "Python keyword arguments require a known user-function signature");
  }
  const auto& associated_callee = expression.children.front();
  const auto& associated_callee_facts = semantic(semantics_, associated_callee);
  if (program_.language == SourceLanguage::fortran && called_function != nullptr) {
    const auto& called_facts = semantic(semantics_, *called_function);
    semantic(semantics_, expression).argument_intents = called_facts.parameter_intents;
    semantic(semantics_, expression).procedure_has_result =
        !called_function->return_names.empty() || called_facts.has_value_return;
  }
  if (associated_callee.kind == ExpressionKind::identifier &&
      associated_callee_facts.binding == BindingKind::builtin &&
      associated_callee_facts.intrinsic == IntrinsicId::present) {
    const bool valid = expression.children.size() == 2 &&
                       expression.children[1].kind == ExpressionKind::identifier &&
                       !function_optional_parameters_.empty() &&
                       names_.reference(expression.children[1].id) != nullptr &&
                       function_optional_parameters_.back().count(
                           names_.reference(expression.children[1].id)->symbol) != 0U;
    if (expression.children.size() == 2 &&
        expression.children[1].kind == ExpressionKind::identifier) {
      if (auto* symbol = lookup(expression.children[1])) {
        auto& argument_facts = semantic(semantics_, expression.children[1]);
        argument_facts.binding = symbol->binding;
        argument_facts.inferred_type = symbol->type;
        argument_facts.element_type = symbol->element_type;
        argument_facts.shape = symbol->shape;
      }
    } else if (expression.children.size() == 2) {
      analyze_expression(expression.children[1]);
    }
    if (!valid) {
      diagnose(expression.location.line, "MPF2040",
               "Fortran PRESENT requires one OPTIONAL dummy argument");
    }
    return semantic(semantics_, expression).inferred_type = ValueType::boolean;
  }
  ValueType argument_type = ValueType::unknown;
  std::unordered_map<SymbolId, std::vector<StorageRegion>> reference_actuals;
  for (std::size_t index = 1; index < expression.children.size(); ++index) {
    auto& argument = expression.children[index];
    const auto intent_index = index - 1;
    const auto intent = intent_index < semantic(semantics_, expression).argument_intents.size()
                            ? semantic(semantics_, expression).argument_intents[intent_index]
                            : ParameterIntent::in;
    const bool writes_actual = intent == ParameterIntent::out || intent == ParameterIntent::inout;
    if (argument.kind == ExpressionKind::omitted_argument) continue;
    if (program_.language == SourceLanguage::fortran && called_function != nullptr &&
        argument.kind == ExpressionKind::identifier && !function_optional_parameters_.empty() &&
        names_.reference(argument.id) != nullptr &&
        function_optional_parameters_.back().count(names_.reference(argument.id)->symbol) != 0U) {
      const auto& called_facts = semantic(semantics_, *called_function);
      const bool target_optional = intent_index < called_facts.parameter_optional.size() &&
                                   called_facts.parameter_optional[intent_index];
      if (target_optional) {
        semantic(semantics_, expression).argument_optional_forward[intent_index] = true;
      } else {
        diagnose(argument.location.line, "MPF2040",
                 "an optional dummy actual requires an optional dummy target");
      }
    }
    if (program_.language == SourceLanguage::fortran && writes_actual) {
      if (argument.kind != ExpressionKind::identifier) {
        argument_type = join_types(argument_type, analyze_expression(argument));
      }
      const auto* root = root_container(argument);
      if (root == nullptr || root->kind != ExpressionKind::identifier ||
          (argument.kind != ExpressionKind::identifier &&
           (argument.kind != ExpressionKind::index ||
            (contains_slice(argument) && !has_direct_slice(argument))))) {
        diagnose(argument.location.line, "MPF2038",
                 "Fortran OUT/INOUT actual argument must be a definable name, element, or section");
        continue;
      }
      const auto* root_use = names_.reference(root->id);
      const auto root_symbol = root_use == nullptr ? SymbolId{} : root_use->symbol;
      auto* symbol = lookup(*root);
      if (symbol == nullptr || symbol->binding != BindingKind::variable) {
        if (argument.kind == ExpressionKind::identifier) analyze_expression(argument);
        diagnose(argument.location.line, "MPF2038",
                 "Fortran OUT/INOUT actual argument is not a variable");
        continue;
      }
      if (argument.kind == ExpressionKind::identifier) {
        if (intent == ParameterIntent::inout) {
          argument_type = join_types(argument_type, analyze_expression(argument));
        } else {
          auto& argument_facts = semantic(semantics_, argument);
          argument_facts.binding = symbol->binding;
          argument_facts.inferred_type = symbol->type;
          argument_facts.element_type = symbol->element_type;
          argument_facts.shape = symbol->shape;
          argument_facts.storage_region = full_storage_region(symbol->shape);
          argument_type = join_types(argument_type, symbol->type);
        }
      }
      auto& regions = reference_actuals[root_symbol];
      const auto& region = semantic(semantics_, argument).storage_region;
      const auto conflicts =
          std::any_of(regions.begin(), regions.end(), [&](const StorageRegion& previous) {
            return storage_region_relation(previous, region) != StorageRegionRelation::disjoint;
          });
      if (conflicts) {
        diagnose(argument.location.line, "MPF2038",
                 "Fortran writable actual argument regions overlap or cannot be proven disjoint");
      }
      regions.push_back(region);
      diagnose_fortran_parameter_write(root_symbol, argument.location.line);
      symbol->assigned = true;
    } else {
      argument_type = join_types(argument_type, analyze_expression(argument));
    }
  }
  if (program_.language == SourceLanguage::fortran && called_function != nullptr) {
    const auto& called_facts = semantic(semantics_, *called_function);
    const auto comparable =
        std::min(expression.children.size() - 1, called_facts.parameter_types.size());
    for (std::size_t index = 0; index < comparable; ++index) {
      const auto& argument = expression.children[index + 1];
      if (argument.kind == ExpressionKind::omitted_argument) continue;
      const auto& argument_facts = semantic(semantics_, argument);
      const auto expected_type = called_facts.parameter_types[index];
      if (expected_type == ValueType::list) {
        if (argument_facts.inferred_type != ValueType::list &&
            argument_facts.inferred_type != ValueType::unknown) {
          diagnose(argument.location.line, "MPF2039",
                   "Fortran array dummy argument requires an array actual");
          continue;
        }
        const auto& expected_shape = called_facts.parameter_shapes[index];
        if (!expected_shape.empty() && !argument_facts.shape.empty() &&
            expected_shape.size() != argument_facts.shape.size()) {
          diagnose(argument.location.line, "MPF2039",
                   "Fortran dummy and actual array ranks do not match");
          continue;
        }
        for (std::size_t dimension = 0;
             dimension < expected_shape.size() && dimension < argument_facts.shape.size();
             ++dimension) {
          if (expected_shape[dimension] != dynamic_extent &&
              argument_facts.shape[dimension] != dynamic_extent &&
              expected_shape[dimension] != argument_facts.shape[dimension]) {
            diagnose(argument.location.line, "MPF2039",
                     "Fortran dummy and actual array extents do not match");
            break;
          }
        }
        const auto expected_element = called_facts.parameter_element_types[index];
        if (expected_element != ValueType::unknown &&
            argument_facts.element_type != ValueType::unknown &&
            join_types(expected_element, argument_facts.element_type) == ValueType::unknown) {
          diagnose(argument.location.line, "MPF2039",
                   "Fortran dummy and actual array element types do not match");
        }
      } else if (expected_type != ValueType::unknown &&
                 argument_facts.inferred_type == ValueType::list) {
        diagnose(argument.location.line, "MPF2039",
                 "Fortran scalar dummy argument cannot receive an array actual");
      }
    }
  }
  if (associated_callee.kind == ExpressionKind::identifier &&
      associated_callee_facts.binding == BindingKind::builtin) {
    if (associated_callee_facts.intrinsic == IntrinsicId::python_float) {
      if (expression.children.size() != 2) {
        diagnose(expression.location.line, "MPF2033", "Python float requires exactly one argument");
      } else {
        const auto type = semantic(semantics_, expression.children[1]).inferred_type;
        if (type != ValueType::integer && type != ValueType::real && type != ValueType::boolean &&
            type != ValueType::string && type != ValueType::unknown) {
          diagnose(expression.location.line, "MPF2033",
                   "Python float argument is not convertible in the current subset");
        }
      }
      return semantic(semantics_, expression).inferred_type = ValueType::real;
    }
    if (associated_callee_facts.intrinsic == IntrinsicId::python_length ||
        associated_callee_facts.intrinsic == IntrinsicId::matlab_length ||
        associated_callee_facts.intrinsic == IntrinsicId::element_count) {
      if (expression.children.size() != 2) {
        diagnose(expression.location.line, "MPF2026", "length/size builtin requires one argument");
      } else if (semantic(semantics_, expression.children[1]).inferred_type != ValueType::list &&
                 semantic(semantics_, expression.children[1]).inferred_type != ValueType::unknown) {
        diagnose(expression.location.line, "MPF2022", "length/size argument is not an array/list");
      }
      return semantic(semantics_, expression).inferred_type = ValueType::integer;
    }
    if (associated_callee_facts.intrinsic == IntrinsicId::sum && expression.children.size() == 2) {
      const auto& argument_facts = semantic(semantics_, expression.children[1]);
      if (argument_facts.inferred_type != ValueType::list &&
          argument_facts.inferred_type != ValueType::unknown) {
        diagnose(expression.location.line, "MPF2022", "sum argument is not an array/list");
      } else if (argument_facts.shape.size() > 1 && program_.language != SourceLanguage::fortran) {
        diagnose(expression.location.line, "MPF2028",
                 "multidimensional SUM semantics are not supported for this source language");
      }
      return semantic(semantics_, expression).inferred_type =
                 argument_facts.element_type == ValueType::boolean ? ValueType::integer
                                                                   : argument_facts.element_type;
    }
    if (associated_callee_facts.intrinsic == IntrinsicId::sum && expression.children.size() != 2) {
      diagnose(expression.location.line, "MPF2026", "sum builtin requires one argument");
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    }
    if (associated_callee_facts.intrinsic == IntrinsicId::reshape) {
      return analyze_reshape(expression);
    }
    if (associated_callee_facts.intrinsic == IntrinsicId::absolute ||
        associated_callee_facts.intrinsic == IntrinsicId::minimum ||
        associated_callee_facts.intrinsic == IntrinsicId::maximum) {
      return semantic(semantics_, expression).inferred_type = argument_type;
    }
    return semantic(semantics_, expression).inferred_type = ValueType::real;
  }
  if (associated_callee_facts.binding == BindingKind::variable &&
      associated_callee_facts.inferred_type == ValueType::list) {
    diagnose(expression.location.line, "MPF2025", "array/list indexing syntax is invalid");
  }
  if (called_function != nullptr) {
    const auto* function = called_function;
    const auto& function_facts = semantic(semantics_, *function);
    const auto argument_count = expression.children.size() - 1;
    if (argument_count != function->parameters.size()) {
      diagnose(expression.location.line, "MPF2034",
               "function '" + associated_callee.value + "' expects " +
                   std::to_string(function->parameters.size()) + " input arguments but received " +
                   std::to_string(argument_count));
    }
    if (program_.language == SourceLanguage::fortran) {
      const bool call_statement = fortran_call_expression_ == &expression;
      const bool subroutine = function->return_names.empty();
      if (call_statement != subroutine) {
        diagnose(expression.location.line, "MPF2037",
                 call_statement ? "Fortran CALL requires a SUBROUTINE"
                                : "Fortran SUBROUTINE references require a CALL statement");
      }
    }
    if (program_.language == SourceLanguage::matlab && function->return_names.size() > 1) {
      semantic(semantics_, expression).multi_output_call = true;
      semantic(semantics_, expression).tuple_types = function_facts.return_types;
      semantic(semantics_, expression).tuple_element_types = function_facts.return_element_types;
      semantic(semantics_, expression).tuple_shapes = function_facts.return_shapes;
      if (semantic(semantics_, expression).requested_outputs > function->return_names.size()) {
        diagnose(expression.location.line, "MPF2034",
                 "function '" + associated_callee.value + "' provides " +
                     std::to_string(function->return_names.size()) +
                     " outputs but the assignment requests " +
                     std::to_string(semantic(semantics_, expression).requested_outputs));
      }
      if (semantic(semantics_, expression).requested_outputs == 1 &&
          !function_facts.return_types.empty()) {
        semantic(semantics_, expression).element_type = function_facts.return_element_types.front();
        semantic(semantics_, expression).shape = function_facts.return_shapes.front();
        return semantic(semantics_, expression).inferred_type = function_facts.return_types.front();
      }
      return semantic(semantics_, expression).inferred_type = ValueType::tuple;
    }
    if (program_.language == SourceLanguage::python &&
        function_facts.declared_type == ValueType::tuple) {
      semantic(semantics_, expression).tuple_types = function_facts.return_types;
      semantic(semantics_, expression).tuple_element_types = function_facts.return_element_types;
      semantic(semantics_, expression).tuple_shapes = function_facts.return_shapes;
    }
    if (program_.language == SourceLanguage::python &&
        (function_facts.declared_type == ValueType::tuple ||
         function_facts.declared_type == ValueType::list)) {
      semantic(semantics_, expression).element_type = function_facts.element_type;
      semantic(semantics_, expression).shape = function_facts.shape;
      semantic(semantics_, expression).sequence_is_list = function_facts.return_sequence_is_list;
      semantic(semantics_, expression).sequence_elements = function_facts.return_sequence_elements;
    }
    return semantic(semantics_, expression).inferred_type = function_facts.declared_type;
  }
  return semantic(semantics_, expression).inferred_type = ValueType::unknown;
}

ValueType Analyzer::analyze_index(Expression& expression, const bool container_already_analyzed,
                                  const bool allow_matlab_growth) {
  if (expression.children.size() < 2) {
    diagnose(expression.location.line, "MPF2025", "index expression requires at least one index");
    return semantic(semantics_, expression).inferred_type = ValueType::unknown;
  }
  auto& container = expression.children[0];
  if (!container_already_analyzed) analyze_expression(container);
  const auto& container_facts = semantic(semantics_, container);
  if (container_facts.inferred_type != ValueType::list &&
      container_facts.inferred_type != ValueType::unknown) {
    diagnose(expression.location.line, "MPF2022", "indexed expression is not an array/list");
    return semantic(semantics_, expression).inferred_type = ValueType::unknown;
  }
  const auto index_count = expression.children.size() - 1;
  if (!container_facts.shape.empty() && index_count > container_facts.shape.size()) {
    diagnose(expression.location.line, "MPF2025", "too many indexes for array/list shape");
  }
  if (program_.language == SourceLanguage::fortran && !container_facts.shape.empty() &&
      index_count != container_facts.shape.size()) {
    diagnose(expression.location.line, "MPF2025",
             "Fortran array reference rank does not match its declared rank");
  }
  bool has_expanding_selector = false;
  std::vector<std::size_t> result_shape;
  auto& index_selectors = semantic(semantics_, expression).index_selectors;
  index_selectors.clear();
  index_selectors.reserve(index_count);
  auto& index_extents = semantic(semantics_, expression).index_extents;
  index_extents.clear();
  index_extents.reserve(index_count);
  StorageRegion storage_region;
  storage_region.kind = semantic(semantics_, expression).column_major && index_count == 1U &&
                                container_facts.shape.size() > 1U
                            ? StorageRegionKind::linearized
                            : StorageRegionKind::rectangular;
  storage_region.root_shape = container_facts.shape;
  bool exact_region = known_shape(container_facts.shape);
  for (std::size_t position = 0; position < index_count; ++position) {
    auto& index = expression.children[position + 1];
    std::size_t extent = dynamic_extent;
    if (semantic(semantics_, expression).column_major && index_count == 1 &&
        container_facts.shape.size() > 1) {
      extent = 1;
      for (const auto dimension : container_facts.shape) {
        if (dimension == dynamic_extent) {
          extent = dynamic_extent;
          break;
        }
        if (dimension != 0 && extent > std::numeric_limits<std::size_t>::max() / dimension) {
          diagnose(expression.location.line, "MPF2027", "array shape exceeds target size limits");
          extent = dynamic_extent;
          break;
        }
        extent *= dimension;
      }
    } else if (position < container_facts.shape.size()) {
      extent = container_facts.shape[position];
    }
    semantic::IndexExtentSource selector_extent = semantic::IndexExtentSource::none;
    const auto dynamic_source = semantic(semantics_, expression).column_major && index_count == 1U
                                    ? semantic::IndexExtentSource::runtime_linear
                                    : semantic::IndexExtentSource::runtime_axis;
    const auto resolve_end = [&](auto&& self, Expression& candidate) -> void {
      if (candidate.kind == ExpressionKind::end_index) {
        if (extent == dynamic_extent) {
          semantic(semantics_, candidate).index_extent = dynamic_source;
          semantic(semantics_, candidate).inferred_type = ValueType::integer;
          selector_extent = dynamic_source;
          return;
        }
        semantic(semantics_, candidate).index_extent = semantic::IndexExtentSource::none;
        candidate.kind = ExpressionKind::number_literal;
        candidate.value = std::to_string(extent);
        candidate.unary_operation = UnaryOperator::none;
        candidate.operation = BinaryOperator::none;
        candidate.comparison = ComparisonOperator::none;
        candidate.comparisons.clear();
        return;
      }
      if (candidate.kind != ExpressionKind::unary && candidate.kind != ExpressionKind::binary &&
          candidate.kind != ExpressionKind::slice && candidate.kind != ExpressionKind::list &&
          candidate.kind != ExpressionKind::tuple) {
        return;
      }
      for (auto& child : candidate.children) self(self, child);
    };
    resolve_end(resolve_end, index);
    index_extents.push_back(selector_extent);
    if (index.kind == ExpressionKind::slice) {
      has_expanding_selector = true;
      index_selectors.push_back(semantic::IndexSelectorKind::slice);
      const auto count = analyze_slice(index, extent, allow_matlab_growth);
      result_shape.push_back(count);
      const auto dimension =
          slice_region_dimension(index, semantic(semantics_, index), extent, count);
      if (dimension.has_value())
        storage_region.dimensions.push_back(*dimension);
      else
        exact_region = false;
      continue;
    }
    const auto index_type = analyze_expression(index);
    const auto& index_facts = semantic(semantics_, index);
    if (program_.language == SourceLanguage::matlab && index_type == ValueType::list) {
      has_expanding_selector = true;
      exact_region = false;
      const auto selector_size = static_element_count(index_facts.shape);
      if (selector_size.has_value() && *selector_size == 0U) {
        index_selectors.push_back(semantic::IndexSelectorKind::empty);
        result_shape.push_back(0U);
        continue;
      }
      if (index_facts.element_type == ValueType::boolean) {
        index_selectors.push_back(semantic::IndexSelectorKind::logical);
        const auto expected_size = index_count == 1U ? static_element_count(container_facts.shape)
                                   : extent == dynamic_extent ? std::optional<std::size_t>{}
                                                              : std::optional<std::size_t>{extent};
        if (expected_size.has_value() && selector_size.has_value() &&
            *expected_size != *selector_size) {
          diagnose(index.location.line, "MPF2049",
                   index_count == 1U
                       ? "Matlab linear logical selector must contain one value per array element"
                       : "Matlab dimensional logical selector must match its array extent");
        }
        result_shape.push_back(boolean_literal_count(index).value_or(dynamic_extent));
        continue;
      }
      if (index_facts.element_type == ValueType::integer ||
          index_facts.element_type == ValueType::real) {
        index_selectors.push_back(semantic::IndexSelectorKind::numeric);
        result_shape.push_back(selector_size.value_or(dynamic_extent));
        const auto validate_elements = [&](auto&& self, const Expression& candidate) -> void {
          if (candidate.kind == ExpressionKind::list) {
            for (const auto& child : candidate.children) self(self, child);
            return;
          }
          if (numeric_constant(candidate).has_value() &&
              !integral_constant(candidate).has_value()) {
            diagnose(candidate.location.line, "MPF2023",
                     "Matlab numeric selector arrays must contain integer values");
            return;
          }
          validate_static_index(candidate.location.line, candidate, extent,
                                semantic(semantics_, expression).index_base,
                                semantic(semantics_, expression).allow_negative_index,
                                allow_matlab_growth);
        };
        validate_elements(validate_elements, index);
        continue;
      }
      index_selectors.push_back(semantic::IndexSelectorKind::numeric);
      result_shape.push_back(selector_size.value_or(dynamic_extent));
      diagnose(index.location.line, "MPF2023",
               "Matlab numeric selector arrays must contain integer values");
      exact_region = false;
      continue;
    }
    index_selectors.push_back(semantic::IndexSelectorKind::scalar);
    const bool typescript_integral_number = program_.language == SourceLanguage::typescript &&
                                            index_type == ValueType::real &&
                                            integral_constant(index).has_value();
    if (index_type != ValueType::integer && index_type != ValueType::unknown &&
        !typescript_integral_number) {
      diagnose(index.location.line, "MPF2023", "array/list index must be an integer");
    }
    validate_static_index(
        index.location.line, index, extent, semantic(semantics_, expression).index_base,
        semantic(semantics_, expression).allow_negative_index, allow_matlab_growth);
    const auto dimension =
        scalar_region_dimension(index, extent, semantic(semantics_, expression).index_base,
                                semantic(semantics_, expression).allow_negative_index);
    if (dimension.has_value())
      storage_region.dimensions.push_back(*dimension);
    else
      exact_region = false;
  }
  if (storage_region.kind == StorageRegionKind::rectangular && exact_region) {
    for (std::size_t position = index_count; position < container_facts.shape.size(); ++position) {
      storage_region.dimensions.push_back({0U, 1U, container_facts.shape[position]});
    }
  }
  if (exact_region && valid_storage_region(storage_region)) {
    semantic(semantics_, expression).storage_region = std::move(storage_region);
  } else {
    semantic(semantics_, expression).storage_region = {};
  }
  semantic(semantics_, expression).element_type = container_facts.element_type;
  if (container_facts.shape.empty()) {
    if (has_expanding_selector) {
      semantic(semantics_, expression).shape = std::move(result_shape);
      return semantic(semantics_, expression).inferred_type = ValueType::list;
    }
    return semantic(semantics_, expression).inferred_type = ValueType::unknown;
  }
  if (!(semantic(semantics_, expression).column_major && index_count == 1 &&
        container_facts.shape.size() > 1) &&
      index_count < container_facts.shape.size()) {
    result_shape.insert(result_shape.end(),
                        container_facts.shape.begin() + static_cast<std::ptrdiff_t>(index_count),
                        container_facts.shape.end());
  }
  if (has_expanding_selector || !result_shape.empty()) {
    semantic(semantics_, expression).shape = std::move(result_shape);
    return semantic(semantics_, expression).inferred_type = ValueType::list;
  }
  return semantic(semantics_, expression).inferred_type = container_facts.element_type;
}

void Analyzer::analyze_indexed_mutation(Statement& statement, const ValueType value_type,
                                        const ValueType target_type) {
  auto& statement_facts = semantic(semantics_, statement);
  auto& target = statement.target_expression;
  auto& target_facts = semantic(semantics_, target);
  auto& contract = statement_facts.indexed_mutation;
  contract.kind = semantic::IndexedMutationKind::overwrite;
  contract.shape_source = semantic::IndexedMutationShapeSource::preserve;
  contract.linear = target_facts.column_major && target.children.size() == 2U;

  if (target.kind != ExpressionKind::index || target.children.size() < 2U) return;
  const auto& container_facts = semantic(semantics_, target.children.front());
  const auto selector_count = target.children.size() - 1U;
  statement_facts.mutation_input_shape =
      container_facts.shape.empty()
          ? std::vector<std::size_t>(std::max<std::size_t>(1U, selector_count), dynamic_extent)
          : container_facts.shape;
  statement_facts.mutation_result_shape = statement_facts.mutation_input_shape;

  if (program_.language == SourceLanguage::python &&
      std::any_of(target_facts.index_selectors.begin(), target_facts.index_selectors.end(),
                  semantic::selector_preserves_dimension)) {
    contract.kind = semantic::IndexedMutationKind::resize;
    contract.shape_source = semantic::IndexedMutationShapeSource::runtime_selectors;
    statement_facts.mutation_result_shape.assign(statement_facts.mutation_input_shape.size(),
                                                 dynamic_extent);
    return;
  }
  if (program_.language != SourceLanguage::matlab) return;

  const bool erase = value_type == ValueType::list && empty_list_literal(statement.expression);
  const auto input_shape = statement_facts.mutation_input_shape;
  auto result_shape = input_shape;
  const bool input_shape_known = known_shape(input_shape);

  if (erase) {
    contract.kind = semantic::IndexedMutationKind::erase;
    std::optional<std::size_t> axis;
    std::size_t selector_position = 0U;
    if (contract.linear) {
      axis = matlab_vector_axis(input_shape);
      if (!axis.has_value()) {
        diagnose(statement.line, "MPF2050",
                 "Matlab linear indexed deletion requires a row or column vector");
        return;
      }
    } else {
      if (selector_count != input_shape.size()) {
        diagnose(statement.line, "MPF2050",
                 "Matlab dimensional deletion requires one selector per array dimension");
        return;
      }
      for (std::size_t position = 0; position < selector_count; ++position) {
        if (full_slice(target.children[position + 1U])) continue;
        if (axis.has_value()) {
          diagnose(statement.line, "MPF2050",
                   "Matlab null assignment may have only one non-colon selector");
          return;
        }
        axis = position;
        selector_position = position;
      }
      if (!axis.has_value()) {
        diagnose(statement.line, "MPF2050",
                 "Matlab indexed deletion requires a non-colon selector dimension");
        return;
      }
    }
    contract.axis = *axis;
    const auto& selector = target.children[selector_position + 1U];
    const auto kind = target_facts.index_selectors[selector_position];
    const auto selection_extent = contract.linear
                                      ? static_element_count(input_shape).value_or(dynamic_extent)
                                      : input_shape[*axis];
    const auto required = selector_required_extent(selector, kind, selection_extent);
    if (required.has_value() && selection_extent != dynamic_extent &&
        *required > selection_extent) {
      diagnose(statement.line, "MPF2021", "Matlab indexed deletion selector is out of bounds");
    }
    const auto erased = selector_erased_count(selector, kind, semantics_, selection_extent);
    if (!input_shape_known || !erased.has_value() || selection_extent == dynamic_extent) {
      contract.shape_source = semantic::IndexedMutationShapeSource::runtime_selectors;
      result_shape[*axis] = dynamic_extent;
    } else if (*erased > result_shape[*axis]) {
      diagnose(statement.line, "MPF2021", "Matlab indexed deletion selector is out of bounds");
    } else {
      contract.shape_source = semantic::IndexedMutationShapeSource::static_extents;
      result_shape[*axis] -= *erased;
    }
    statement_facts.declared_type = ValueType::list;
    statement_facts.element_type = container_facts.element_type;
    statement_facts.shape = result_shape;
    statement_facts.mutation_result_shape = result_shape;
    if (const auto* root = root_container(target); root != nullptr) {
      if (auto* symbol = lookup(*root)) symbol->shape = result_shape;
    }
    return;
  }

  bool runtime_shape = !input_shape_known;
  bool may_grow = false;
  if (contract.linear) {
    const auto current_size = static_element_count(input_shape).value_or(dynamic_extent);
    const auto required =
        selector_required_extent(target.children[1], target_facts.index_selectors[0], current_size);
    runtime_shape = runtime_shape || !required.has_value();
    may_grow = runtime_shape || (current_size != dynamic_extent && *required > current_size);
    if (may_grow && !runtime_shape) {
      const auto required_size = *required;
      if (input_shape.size() == 1U) {
        result_shape[0] = std::max(result_shape[0], required_size);
      } else if (input_shape == std::vector<std::size_t>{0U, 0U}) {
        // Linear assignment grows Matlab's canonical 0-by-0 empty array as a
        // row vector, matching the orientation Matlab chooses for A(k)=value.
        result_shape = {1U, required_size};
      } else if (const auto vector_axis = matlab_vector_axis(input_shape);
                 vector_axis.has_value()) {
        result_shape[*vector_axis] = std::max(result_shape[*vector_axis], required_size);
      } else {
        std::size_t leading = 1U;
        for (std::size_t axis = 0; axis + 1U < input_shape.size(); ++axis) {
          if (input_shape[axis] != 0U &&
              leading > std::numeric_limits<std::size_t>::max() / input_shape[axis]) {
            runtime_shape = true;
            break;
          }
          leading *= input_shape[axis];
        }
        if (!runtime_shape && leading != 0U) {
          result_shape.back() =
              std::max(result_shape.back(), (required_size + leading - 1U) / leading);
        }
      }
    }
  } else {
    for (std::size_t position = 0; position < selector_count; ++position) {
      const auto current_extent =
          position < input_shape.size() ? input_shape[position] : dynamic_extent;
      const auto required = selector_required_extent(
          target.children[position + 1U], target_facts.index_selectors[position], current_extent);
      if (!required.has_value() || current_extent == dynamic_extent) {
        runtime_shape = true;
        may_grow = true;
      } else if (*required > current_extent) {
        may_grow = true;
        if (position < result_shape.size()) result_shape[position] = *required;
      }
    }
    if (may_grow && selector_count != input_shape.size()) {
      diagnose(statement.line, "MPF2050",
               "Matlab shape-changing dimensional assignment requires one selector per dimension");
      return;
    }
  }

  if (!may_grow) return;
  contract.kind = semantic::IndexedMutationKind::grow;
  contract.shape_source = runtime_shape ? semantic::IndexedMutationShapeSource::runtime_selectors
                                        : semantic::IndexedMutationShapeSource::static_extents;
  if (runtime_shape) {
    result_shape.assign(input_shape.size(), dynamic_extent);
  }
  statement_facts.declared_type = ValueType::list;
  statement_facts.element_type = container_facts.element_type != ValueType::unknown
                                     ? container_facts.element_type
                                     : target_type;
  statement_facts.shape = result_shape;
  statement_facts.mutation_result_shape = result_shape;
  if (const auto* root = root_container(target); root != nullptr) {
    if (auto* symbol = lookup(*root)) symbol->shape = result_shape;
  }
}

void Analyzer::analyze_section_assignment(Statement& statement, const ValueType value_type) {
  auto& target = statement.target_expression;
  auto& target_facts = semantic(semantics_, target);
  const bool replacement_is_list = value_type == ValueType::list;
  const auto replacement_element =
      replacement_is_list ? semantic(semantics_, statement.expression).element_type : value_type;
  if (program_.language == SourceLanguage::python && !replacement_is_list) {
    diagnose(statement.line, "MPF2031",
             "Python slice assignment requires a list replacement in the current subset");
  }
  if (target_facts.element_type != ValueType::unknown &&
      replacement_element != ValueType::unknown &&
      join_types(target_facts.element_type, replacement_element) == ValueType::unknown) {
    diagnose(statement.line, "MPF2020", "section assignment changes the array element type");
  }

  if (replacement_is_list) {
    const auto normalized_replacement = assignment_shape(
        semantic(semantics_, statement.expression).shape, target_facts.shape.size());
    if (program_.language == SourceLanguage::python) {
      bool extended_slice = false;
      for (std::size_t index = 1; index < target.children.size(); ++index) {
        const auto& selector = target.children[index];
        if (selector.kind != ExpressionKind::slice || selector.children.size() != 3 ||
            !selector.children[2].valid())
          continue;
        const auto step = numeric_constant(selector.children[2]);
        if (step.has_value() && *step != 1.0) extended_slice = true;
      }
      if (extended_slice && !target_facts.shape.empty() && !normalized_replacement.empty() &&
          target_facts.shape.front() != dynamic_extent &&
          normalized_replacement.front() != dynamic_extent &&
          target_facts.shape.front() != normalized_replacement.front()) {
        diagnose(
            statement.line, "MPF2031",
            "Python extended slice assignment requires the same number of replacement elements");
      }
    } else if (known_shape(target_facts.shape) && known_shape(normalized_replacement) &&
               target_facts.shape != normalized_replacement) {
      diagnose(statement.line, "MPF2031",
               "section assignment replacement shape is not conformable with the selected shape");
    }
  }

  if (program_.language == SourceLanguage::python) {
    const auto* root = root_container(target);
    if (root != nullptr && root->kind == ExpressionKind::identifier) {
      if (auto* symbol = lookup(*root)) symbol->shape.clear();
    }
  }
}

std::size_t Analyzer::analyze_slice(Expression& slice, const std::size_t extent,
                                    const bool allow_matlab_growth) {
  auto& slice_facts = semantic(semantics_, slice);
  slice_facts.inferred_type = ValueType::list;
  for (auto& bound : slice.children) {
    if (!bound.valid()) continue;
    const auto type = analyze_expression(bound);
    if (type != ValueType::integer && type != ValueType::unknown) {
      diagnose(bound.location.line, "MPF2023", "slice bounds and step must be integers");
    }
  }
  if (slice.children.size() != 3 || extent == dynamic_extent) return dynamic_extent;
  const auto start_value =
      slice.children[0].valid() ? integral_constant(slice.children[0]) : std::optional<long long>{};
  const auto stop_value =
      slice.children[1].valid() ? integral_constant(slice.children[1]) : std::optional<long long>{};
  const auto step_value =
      slice.children[2].valid() ? integral_constant(slice.children[2]) : std::optional<long long>{};
  for (std::size_t position = 0; position < slice.children.size(); ++position) {
    const auto& bound = slice.children[position];
    const auto normalized = position == 0 ? start_value : position == 1 ? stop_value : step_value;
    if (bound.valid() && numeric_constant(bound).has_value() && !normalized.has_value()) {
      diagnose(bound.location.line, "MPF2027", "slice bound or step exceeds target integer range");
    }
  }
  if ((slice.children[0].valid() && !start_value.has_value()) ||
      (slice.children[1].valid() && !stop_value.has_value()) ||
      (slice.children[2].valid() && !step_value.has_value()))
    return dynamic_extent;
  const auto step = step_value.value_or(1);
  if (step == 0) {
    diagnose(slice.location.line, "MPF2030", "slice step cannot be zero");
    return 0;
  }
  if (step == LLONG_MIN) {
    diagnose(slice.location.line, "MPF2027", "slice step exceeds target integer range");
    return dynamic_extent;
  }
  if (extent == 0) return 0;

  if (!slice_facts.slice_stop_inclusive) {
    const auto size = static_cast<long long>(extent);
    auto start = start_value.value_or(step > 0 ? 0 : size - 1);
    auto stop = stop_value.value_or(step > 0 ? size : -1);
    if (start_value.has_value() && start < 0) start += size;
    if (stop_value.has_value() && stop < 0) stop += size;
    if (step > 0) {
      start = std::max(0LL, std::min(start, size));
      stop = std::max(0LL, std::min(stop, size));
      return start < stop ? static_cast<std::size_t>((stop - start - 1) / step + 1) : 0U;
    }
    start = std::max(-1LL, std::min(start, size - 1));
    stop = std::max(-1LL, std::min(stop, size - 1));
    return start > stop ? static_cast<std::size_t>((start - stop - 1) / (-step) + 1) : 0U;
  }

  const auto first =
      start_value.value_or(step > 0 ? static_cast<long long>(slice_facts.index_base)
                                    : static_cast<long long>(slice_facts.index_base + extent - 1));
  const auto stop =
      stop_value.value_or(step > 0 ? static_cast<long long>(slice_facts.index_base + extent - 1)
                                   : static_cast<long long>(slice_facts.index_base));
  if ((step > 0 && first > stop) || (step < 0 && first < stop)) return 0;
  const auto distance = step > 0 ? stop - first : first - stop;
  const auto count = static_cast<std::size_t>(distance / std::llabs(step) + 1);
  Expression first_expression;
  first_expression.kind = ExpressionKind::number_literal;
  first_expression.value = std::to_string(first);
  first_expression.location = slice.location;
  Expression last_expression = first_expression;
  last_expression.value = std::to_string(first + static_cast<long long>(count - 1) * step);
  validate_static_index(slice.location.line, first_expression, extent, slice_facts.index_base,
                        slice_facts.allow_negative_index, allow_matlab_growth);
  validate_static_index(slice.location.line, last_expression, extent, slice_facts.index_base,
                        slice_facts.allow_negative_index, allow_matlab_growth);
  slice_facts.shape = {count};
  return count;
}

void Analyzer::validate_static_index(const std::size_t line, const Expression& index,
                                     const std::size_t extent, const std::size_t base,
                                     const bool allow_negative, const bool allow_matlab_growth) {
  if (extent == dynamic_extent) return;
  const auto constant = numeric_constant(index);
  if (!constant.has_value()) return;
  constexpr double maximum_safe_integer = 9007199254740991.0;
  if (std::fabs(*constant) > maximum_safe_integer) {
    diagnose(line, "MPF2021", "constant array/list index exceeds the target-safe integer range");
    return;
  }
  const auto integral = integral_constant(index);
  const auto minimum = static_cast<double>(std::numeric_limits<long long>::min());
  if (!integral.has_value() && (*constant < minimum || *constant >= -minimum)) {
    diagnose(line, "MPF2021", "constant array/list index is out of bounds");
    return;
  }
  if (!integral.has_value()) return;
  if (allow_matlab_growth && !allow_negative) {
    if (*integral < 0 || static_cast<unsigned long long>(*integral) < base) {
      diagnose(line, "MPF2021", "constant array/list index is out of bounds");
    }
    return;
  }
  if (!normalized_index(*integral, extent, base, allow_negative).has_value()) {
    diagnose(line, "MPF2021", "constant array/list index is out of bounds");
  }
}

ValueType Analyzer::analyze_reshape(Expression& expression) {
  const bool matlab_dimensions =
      program_.language == SourceLanguage::matlab && expression.children.size() > 3;
  if ((!matlab_dimensions && expression.children.size() != 3) || expression.children.size() < 3) {
    diagnose(expression.location.line, "MPF2026",
             "RESHAPE requires a source and a non-empty shape vector/dimension list");
    return semantic(semantics_, expression).inferred_type = ValueType::unknown;
  }
  auto& source = expression.children[1];
  const auto& source_facts = semantic(semantics_, source);
  if (source_facts.inferred_type != ValueType::list ||
      (!matlab_dimensions &&
       semantic(semantics_, expression.children[2]).inferred_type != ValueType::list)) {
    diagnose(expression.location.line, "MPF2022",
             "RESHAPE source and shape-vector arguments must be arrays/lists");
    return semantic(semantics_, expression).inferred_type = ValueType::unknown;
  }
  std::vector<std::size_t> dimensions;
  std::vector<const Expression*> dimension_expressions;
  if (matlab_dimensions) {
    for (std::size_t index = 2; index < expression.children.size(); ++index) {
      dimension_expressions.push_back(&expression.children[index]);
    }
  } else {
    for (const auto& dimension : expression.children[2].children) {
      dimension_expressions.push_back(&dimension);
    }
  }
  for (const auto* dimension : dimension_expressions) {
    const auto value = numeric_constant(*dimension);
    if (!value.has_value() || *value < 0.0 ||
        *value > static_cast<double>(std::numeric_limits<std::size_t>::max()) ||
        *value != static_cast<double>(static_cast<std::size_t>(*value))) {
      diagnose(dimension->location.line, "MPF2027",
               "RESHAPE dimensions must be non-negative integer constants");
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    }
    dimensions.push_back(static_cast<std::size_t>(*value));
  }
  if (dimensions.empty()) {
    diagnose(expression.location.line, "MPF2027", "RESHAPE requires at least one result dimension");
    return semantic(semantics_, expression).inferred_type = ValueType::unknown;
  }
  std::size_t source_size = 1;
  bool source_size_known = !source_facts.shape.empty() && known_shape(source_facts.shape);
  for (const auto dimension : source_facts.shape) {
    if (!source_size_known) break;
    if (dimension != 0 && source_size > std::numeric_limits<std::size_t>::max() / dimension) {
      diagnose(expression.location.line, "MPF2027", "RESHAPE source exceeds target size limits");
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    }
    source_size *= dimension;
  }
  std::size_t result_size = 1;
  for (const auto dimension : dimensions) {
    if (dimension != 0 && result_size > std::numeric_limits<std::size_t>::max() / dimension) {
      diagnose(expression.location.line, "MPF2027", "RESHAPE result exceeds target size limits");
      return semantic(semantics_, expression).inferred_type = ValueType::unknown;
    }
    result_size *= dimension;
  }
  if (source_size_known && source_size != result_size) {
    diagnose(expression.location.line, "MPF2024",
             "RESHAPE source size does not match result shape");
  }
  semantic(semantics_, expression).inferred_type = ValueType::list;
  semantic(semantics_, expression).element_type = source_facts.element_type;
  semantic(semantics_, expression).shape = std::move(dimensions);
  semantic(semantics_, expression).column_major = true;
  return semantic(semantics_, expression).inferred_type;
}

}  // namespace mpf::detail::semantic_internal
