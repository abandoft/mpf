#include "argument_validator_catalog.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace mpf::detail {
namespace {

constexpr std::array<ArgumentValidatorDefinition, 28> definitions{
    {{ArgumentValidator::numeric, "mustBeNumeric", {2019, 2}, "R2019b"},
     {ArgumentValidator::numeric_or_logical, "mustBeNumericOrLogical", {2019, 2}, "R2019b"},
     {ArgumentValidator::floating, "mustBeFloat", {2020, 2}, "R2020b"},
     {ArgumentValidator::real, "mustBeReal", {2019, 2}, "R2019b"},
     {ArgumentValidator::finite, "mustBeFinite", {2019, 2}, "R2019b"},
     {ArgumentValidator::non_nan, "mustBeNonNan", {2019, 2}, "R2019b"},
     {ArgumentValidator::positive, "mustBePositive", {2019, 2}, "R2019b"},
     {ArgumentValidator::nonpositive, "mustBeNonpositive", {2019, 2}, "R2019b"},
     {ArgumentValidator::nonnegative, "mustBeNonnegative", {2019, 2}, "R2019b"},
     {ArgumentValidator::negative, "mustBeNegative", {2019, 2}, "R2019b"},
     {ArgumentValidator::nonzero, "mustBeNonzero", {2019, 2}, "R2019b"},
     {ArgumentValidator::integer, "mustBeInteger", {2019, 2}, "R2019b"},
     {ArgumentValidator::nonempty, "mustBeNonempty", {2019, 2}, "R2019b"},
     {ArgumentValidator::scalar_or_empty, "mustBeScalarOrEmpty", {2020, 2}, "R2020b"},
     {ArgumentValidator::vector, "mustBeVector", {2020, 2}, "R2020b"},
     {ArgumentValidator::row, "mustBeRow", {2024, 2}, "R2024b"},
     {ArgumentValidator::column, "mustBeColumn", {2024, 2}, "R2024b"},
     {ArgumentValidator::matrix, "mustBeMatrix", {2024, 2}, "R2024b"},
     {ArgumentValidator::nonmissing, "mustBeNonmissing", {2020, 2}, "R2020b"},
     {ArgumentValidator::nonzero_length_text, "mustBeNonzeroLengthText", {2020, 2}, "R2020b"},
     {ArgumentValidator::text, "mustBeText", {2020, 2}, "R2020b"},
     {ArgumentValidator::text_scalar, "mustBeTextScalar", {2020, 2}, "R2020b"},
     {ArgumentValidator::valid_variable_name, "mustBeValidVariableName", {2020, 2}, "R2020b"},
     {ArgumentValidator::greater_than, "mustBeGreaterThan", {2019, 2}, "R2019b"},
     {ArgumentValidator::greater_than_or_equal, "mustBeGreaterThanOrEqual", {2019, 2}, "R2019b"},
     {ArgumentValidator::less_than, "mustBeLessThan", {2019, 2}, "R2019b"},
     {ArgumentValidator::less_than_or_equal, "mustBeLessThanOrEqual", {2019, 2}, "R2019b"},
     {ArgumentValidator::in_range, "mustBeInRange", {2020, 2}, "R2020b"}}};

static_assert(
    [] {
      for (std::size_t index = 0U; index < definitions.size(); ++index)
        if (static_cast<std::size_t>(definitions[index].validator) != index) return false;
      return true;
    }(),
    "validator definition ordinals must agree with their strong enum identities");

// Preserve O(1) enum-to-definition indexing and O(log N) spelling lookup without a static map,
// heap allocation, mutable initialization, or duplicate naming tables.
constexpr auto spelling_order = [] {
  std::array<std::size_t, definitions.size()> order{};
  for (std::size_t index = 0U; index < order.size(); ++index) order[index] = index;
  for (std::size_t index = 1U; index < order.size(); ++index) {
    auto cursor = index;
    while (cursor > 0U && definitions[order[cursor]].name < definitions[order[cursor - 1U]].name) {
      const auto previous = order[cursor - 1U];
      order[cursor - 1U] = order[cursor];
      order[cursor] = previous;
      --cursor;
    }
  }
  return order;
}();

}  // namespace

std::string_view argument_validator_name(const ArgumentValidator validator) noexcept {
  const auto* definition = find_argument_validator(validator);
  return definition == nullptr ? std::string_view{} : definition->name;
}

const ArgumentValidatorDefinition* find_argument_validator(
    const ArgumentValidator validator) noexcept {
  const auto index = static_cast<std::size_t>(validator);
  return index < definitions.size() ? &definitions[index] : nullptr;
}

const ArgumentValidatorDefinition* find_argument_validator(const std::string_view name) noexcept {
  const auto found = std::lower_bound(
      spelling_order.begin(), spelling_order.end(), name,
      [](const auto index, const auto spelling) { return definitions[index].name < spelling; });
  return found != spelling_order.end() && definitions[*found].name == name ? &definitions[*found]
                                                                           : nullptr;
}

}  // namespace mpf::detail
