#include <algorithm>
#include <limits>
#include <utility>

#include "analyzer_internal.hpp"

namespace {
std::vector<std::size_t> output_shape(
    const std::vector<std::size_t>& raw,
    const std::vector<mpf::detail::ArgumentDimensionConstraint>& dimensions,
    const bool source_known) {
  using namespace mpf::detail;
  auto source = raw;
  if (source.empty())
    source = {1U, 1U};
  else if (source.size() == 1U)
    source.insert(source.begin(), 1U);
  std::vector<std::size_t> result;
  result.reserve(dimensions.size());
  std::size_t variable_axis = dimensions.size();
  std::size_t variable_axes = 0U;
  bool matches = source_known && source.size() == dimensions.size();
  for (std::size_t axis = 0U; axis < dimensions.size(); ++axis) {
    const auto constraint = dimensions[axis];
    if (constraint.any) {
      variable_axis = axis;
      ++variable_axes;
    } else if (axis >= source.size() || source[axis] != constraint.extent)
      matches = false;
    result.push_back(constraint.any ? dynamic_extent : constraint.extent);
  }
  if (matches) {
    for (std::size_t axis = 0U; axis < dimensions.size(); ++axis)
      if (dimensions[axis].any) result[axis] = source[axis];
  } else if (source_known && variable_axes == 1U) {
    const auto product = [](const std::vector<std::size_t>& extents, const std::size_t skip) {
      if (std::any_of(extents.begin(), extents.end(),
                      [](const auto extent) { return extent == 0U; }))
        return std::size_t{0U};
      std::size_t size = 1U;
      for (std::size_t axis = 0U; axis < extents.size(); ++axis) {
        if (axis == skip) continue;
        const auto extent = extents[axis];
        if (extent == dynamic_extent || size > std::numeric_limits<std::size_t>::max() / extent)
          return dynamic_extent;
        size *= extent;
      }
      return size;
    };
    const auto count = product(source, source.size());
    const auto fixed = product(result, variable_axis);
    if (count != dynamic_extent && fixed != dynamic_extent &&
        ((fixed == 0U && count == 0U) || (fixed != 0U && count % fixed == 0U)))
      result[variable_axis] = fixed == 0U ? 0U : count / fixed;
  }
  return result;
}
}  // namespace

namespace mpf::detail::semantic_internal {

void Analyzer::normalize_matlab_output_contract(Statement& function) {
  auto& facts = semantic(semantics_, function);
  for (auto& plan : facts.argument_validations) {
    if (plan.direction != ArgumentDirection::output || plan.ordinal >= facts.return_types.size())
      continue;
    const auto ordinal = plan.ordinal;
    auto type = facts.return_types[ordinal];
    const bool source_known = type != ValueType::unknown;
    auto numeric = facts.return_numeric_types[ordinal];
    auto element = facts.return_element_types[ordinal];
    auto element_numeric = facts.return_element_numeric_types[ordinal];
    auto shape = facts.return_shapes[ordinal];
    if (plan.class_constraint == ArgumentClassConstraint::matlab_char) {
      type = ValueType::string;
      numeric = no_numeric_type;
      element = ValueType::unknown;
      element_numeric = no_numeric_type;
      shape.clear();
    } else {
      auto scalar = type == ValueType::list ? element : type;
      auto scalar_numeric = type == ValueType::list ? element_numeric : numeric;
      if (plan.class_constraint == ArgumentClassConstraint::matlab_logical) {
        scalar = ValueType::boolean;
        scalar_numeric = logical_numeric_type;
      } else if (plan.class_constraint == ArgumentClassConstraint::matlab_double) {
        scalar = ValueType::real;
        // A double class constraint does not discard complex storage identity.
        scalar_numeric =
            scalar_numeric.complexity == NumericComplexity::complex ? complex_numeric_type
            : scalar_numeric.complexity == NumericComplexity::real  ? real_numeric_type
                                                                    : unknown_numeric_type;
      }
      if (plan.dimensions_declared) {
        const bool scalar_shape =
            std::all_of(plan.dimensions.begin(), plan.dimensions.end(),
                        [](const auto& axis) { return !axis.any && axis.extent == 1U; });
        if (scalar_shape) {
          type = scalar;
          numeric = scalar_numeric;
          element = ValueType::unknown;
          element_numeric = no_numeric_type;
          shape.clear();
        } else {
          type = ValueType::list;
          numeric = no_numeric_type;
          element = scalar;
          element_numeric = scalar_numeric;
          shape = output_shape(shape, plan.dimensions, source_known);
        }
      } else if (type == ValueType::list) {
        element = scalar;
        element_numeric = scalar_numeric;
      } else {
        type = scalar;
        numeric = scalar_numeric;
      }
    }
    facts.return_types[ordinal] = type;
    facts.return_numeric_types[ordinal] = numeric;
    facts.return_element_types[ordinal] = element;
    facts.return_element_numeric_types[ordinal] = element_numeric;
    if (type != ValueType::list)
      facts.return_array_storage[ordinal] = ArrayStorageFormat::none;
    else if (plan.dimensions_declared || plan.class_constraint != ArgumentClassConstraint::none)
      facts.return_array_storage[ordinal] = ArrayStorageFormat::dense;
    facts.return_shapes[ordinal] = std::move(shape);
    plan.validated_rank = type == ValueType::list ? facts.return_shapes[ordinal].size() : 0U;
  }
}

}  // namespace mpf::detail::semantic_internal
