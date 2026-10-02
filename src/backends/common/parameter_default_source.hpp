#pragma once

#include "compiler/argument_validation.hpp"
#include "ir/parameter_default_flow.hpp"

namespace mpf::detail {

// No target policy is selected here: check only the provenance of verified MIR defaults.
template <typename Statement>
bool valid_parameter_default_sources(const Statement& statement,
                                     const SourceLanguage source_language) noexcept {
  if (source_language != SourceLanguage::matlab || statement.kind != StatementKind::function)
    return statement.source_parameter_defaults.empty();
  std::size_t source_index = 0U;
  std::size_t validation_index = 0U;
  for (std::size_t parameter = 0U; parameter < statement.parameter_defaults.size(); ++parameter) {
    const auto& expression = statement.parameter_defaults[parameter];
    if (!expression.valid()) continue;
    while (
        validation_index < statement.argument_validations.size() &&
        (statement.argument_validations[validation_index].direction != ArgumentDirection::input ||
         statement.argument_validations[validation_index].ordinal < parameter))
      ++validation_index;
    if (source_index >= statement.source_parameter_defaults.size() ||
        validation_index >= statement.argument_validations.size() ||
        parameter >= statement.parameters.size() ||
        parameter >= statement.parameter_optional.size() ||
        !statement.parameter_optional[parameter])
      return false;
    const auto& source = statement.source_parameter_defaults[source_index++];
    const auto& validation = statement.argument_validations[validation_index];
    if (source.parameter != parameter || source.source != expression.origin ||
        validation.direction != ArgumentDirection::input || validation.ordinal != parameter ||
        !validation.has_default || !source.storage.valid() || !source.presence.valid() ||
        !source.initialization.valid() || source.presence == source.initialization ||
        !source.merge_block.valid() || !source.result.valid())
      return false;
  }
  return source_index == statement.source_parameter_defaults.size();
}

}  // namespace mpf::detail
