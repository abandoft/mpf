#include "semantic_argument_outputs.hpp"

#include <algorithm>

namespace mpf::detail::hir {
bool argument_output_contract_matches(const StatementFacts& facts,
                                      const ArgumentValidationPlan& plan) noexcept {
  const auto ordinal = plan.ordinal;
  if (plan.direction != ArgumentDirection::output || ordinal >= facts.return_types.size() ||
      ordinal >= facts.return_numeric_types.size() ||
      ordinal >= facts.return_element_types.size() ||
      ordinal >= facts.return_element_numeric_types.size() ||
      ordinal >= facts.return_array_storage.size() || ordinal >= facts.return_shapes.size())
    return false;
  const auto type = facts.return_types[ordinal];
  const auto& shape = facts.return_shapes[ordinal];
  const bool array = type == ValueType::list;
  if ((!array && facts.return_array_storage[ordinal] != ArrayStorageFormat::none) ||
      (array &&
       (plan.dimensions_declared || plan.class_constraint != ArgumentClassConstraint::none) &&
       facts.return_array_storage[ordinal] != ArrayStorageFormat::dense))
    return false;
  if (plan.validated_rank != (array ? shape.size() : 0U)) return false;
  const auto scalar = array ? facts.return_element_types[ordinal] : type;
  const auto numeric =
      array ? facts.return_element_numeric_types[ordinal] : facts.return_numeric_types[ordinal];
  switch (plan.class_constraint) {
    case ArgumentClassConstraint::matlab_logical:
      if (scalar != ValueType::boolean || numeric != logical_numeric_type) return false;
      break;
    case ArgumentClassConstraint::matlab_double:
      if (scalar != ValueType::real ||
          (numeric != unknown_numeric_type && numeric.value_class != NumericClass::binary64))
        return false;
      break;
    case ArgumentClassConstraint::matlab_char:
      return type == ValueType::string && shape.empty() && plan.validated_rank == 0U;
    case ArgumentClassConstraint::none: break;
  }
  if (!plan.dimensions_declared) return true;
  const bool scalar_shape =
      std::all_of(plan.dimensions.begin(), plan.dimensions.end(),
                  [](const auto& axis) { return !axis.any && axis.extent == 1U; });
  if (scalar_shape) return !array && shape.empty();
  if (!array || shape.size() != plan.dimensions.size()) return false;
  for (std::size_t axis = 0U; axis < shape.size(); ++axis)
    if (!plan.dimensions[axis].any && shape[axis] != plan.dimensions[axis].extent) return false;
  return true;
}
}  // namespace mpf::detail::hir
