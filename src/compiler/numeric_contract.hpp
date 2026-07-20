#pragma once

#include <cstddef>
#include <vector>

#include "expression_ast.hpp"

namespace mpf::detail {

[[nodiscard]] constexpr bool numeric_contract_matches(const ValueType type,
                                                      const NumericType numeric_type) noexcept {
  if (!numeric_type.valid()) return false;
  if (type == ValueType::unknown) return true;
  switch (type) {
    case ValueType::integer:
    case ValueType::real:
    case ValueType::boolean:
      // ValueType is the legacy coarse scalar category. NumericType is authoritative for
      // storage/promotion and may remain intentionally dynamic across function boundaries.
      return numeric_type == unknown_numeric_type || numeric_type.known();
    case ValueType::string:
    case ValueType::null_value:
    case ValueType::list:
    case ValueType::tuple:
    case ValueType::function:
    case ValueType::exception: return numeric_type == no_numeric_type;
    case ValueType::unknown: return true;
  }
  return false;
}

[[nodiscard]] constexpr bool element_numeric_contract_matches(
    const ValueType container_type, const ValueType element_type,
    const NumericType element_numeric_type) noexcept {
  if (!element_numeric_type.valid()) return false;
  if (container_type == ValueType::list) {
    return numeric_contract_matches(element_type, element_numeric_type);
  }
  if (container_type == ValueType::unknown) {
    return element_numeric_type == no_numeric_type ||
           numeric_contract_matches(element_type, element_numeric_type);
  }
  return element_numeric_type == no_numeric_type;
}

[[nodiscard]] inline bool element_numeric_contracts_match(
    const std::vector<ValueType>& container_types, const std::vector<ValueType>& element_types,
    const std::vector<NumericType>& element_numeric_types) noexcept {
  if (container_types.size() != element_types.size() ||
      container_types.size() != element_numeric_types.size()) {
    return false;
  }
  for (std::size_t index = 0; index < container_types.size(); ++index) {
    if (!element_numeric_contract_matches(container_types[index], element_types[index],
                                          element_numeric_types[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline bool numeric_contracts_match(
    const std::vector<ValueType>& types, const std::vector<NumericType>& numeric_types) noexcept {
  if (types.size() != numeric_types.size()) return false;
  for (std::size_t index = 0; index < types.size(); ++index) {
    if (!numeric_contract_matches(types[index], numeric_types[index])) return false;
  }
  return true;
}

template <typename ExpressionFacts>
[[nodiscard]] bool expression_numeric_contract_matches(const ExpressionFacts& facts) noexcept {
  return numeric_contract_matches(facts.inferred_type, facts.numeric_type) &&
         element_numeric_contract_matches(facts.inferred_type, facts.element_type,
                                          facts.element_numeric_type) &&
         numeric_contracts_match(facts.tuple_types, facts.tuple_numeric_types) &&
         element_numeric_contracts_match(facts.tuple_types, facts.tuple_element_types,
                                         facts.tuple_element_numeric_types);
}

template <typename StatementFacts>
[[nodiscard]] bool statement_numeric_contract_matches(const StatementFacts& facts) noexcept {
  return numeric_contract_matches(facts.declared_type, facts.declared_numeric_type) &&
         element_numeric_contract_matches(facts.declared_type, facts.element_type,
                                          facts.element_numeric_type) &&
         numeric_contract_matches(facts.previous_type, facts.previous_numeric_type) &&
         element_numeric_contract_matches(facts.previous_type, facts.previous_element_type,
                                          facts.previous_element_numeric_type) &&
         numeric_contracts_match(facts.parameter_types, facts.parameter_numeric_types) &&
         element_numeric_contracts_match(facts.parameter_types, facts.parameter_element_types,
                                         facts.parameter_element_numeric_types) &&
         numeric_contracts_match(facts.return_types, facts.return_numeric_types) &&
         element_numeric_contracts_match(facts.return_types, facts.return_element_types,
                                         facts.return_element_numeric_types) &&
         numeric_contracts_match(facts.target_types, facts.target_numeric_types) &&
         element_numeric_contracts_match(facts.target_types, facts.target_element_types,
                                         facts.target_element_numeric_types) &&
         numeric_contracts_match(facts.target_previous_types,
                                 facts.target_previous_numeric_types) &&
         element_numeric_contracts_match(facts.target_previous_types,
                                         facts.target_previous_element_types,
                                         facts.target_previous_element_numeric_types);
}

}  // namespace mpf::detail
