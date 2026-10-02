#include "mir_argument_validation.hpp"

#include <algorithm>
#include <unordered_set>

#include "compiler/argument_validator_catalog.hpp"

namespace mpf::detail::mir {

void verify_argument_validator_sources(const Program& program, std::vector<Diagnostic>& diagnostics,
                                       const std::string_view stage) {
  const auto has_sources =
      std::any_of(program.statements.begin(), program.statements.end(), [](const auto& statement) {
        return !statement.argument_validator_sources.empty() ||
               std::any_of(statement.argument_validations.begin(),
                           statement.argument_validations.end(),
                           [](const auto& plan) { return !plan.validators.empty(); });
      });
  if (!has_sources) return;
  std::unordered_set<HirNodeId::value_type> origins;
  origins.reserve(program.expressions.size() + program.statements.size());
  for (const auto& expression : program.expressions) {
    if (expression.origin.valid()) origins.insert(expression.origin.value());
  }
  for (const auto& statement : program.statements) {
    if (statement.origin.valid()) origins.insert(statement.origin.value());
  }
  for (const auto& statement : program.statements) {
    bool valid = statement.argument_validator_sources.empty() ||
                 (program.source_language == SourceLanguage::matlab &&
                  statement.kind == StatementKind::function);
    std::size_t cursor = 0U;
    for (const auto& plan : statement.argument_validations) {
      for (std::size_t index = 0U; index < plan.validators.size(); ++index) {
        if (cursor >= statement.argument_validator_sources.size()) {
          valid = false;
          continue;
        }
        const auto& source = statement.argument_validator_sources[cursor++];
        const auto& validator = plan.validators[index];
        const auto* definition = find_argument_validator(validator.validator);
        const auto version = program.semantics.language_version.automatic()
                                 ? LanguageVersion{2024, 2}
                                 : program.semantics.language_version;
        valid = valid && definition != nullptr && !(version < definition->minimum_version);
        valid = valid && source.direction == plan.direction && source.formal == plan.ordinal &&
                source.validator_index == index && source.validator == validator.validator &&
                source.call == validator.source_call && source.callee == validator.source_callee;
        for (const auto origin : {source.call, source.callee}) {
          if (!origin.valid() || origin.value() > program.hir_node_count ||
              !origins.insert(origin.value()).second)
            valid = false;
        }
      }
    }
    if (cursor != statement.argument_validator_sources.size()) valid = false;
    if (!valid) {
      diagnostics.push_back(
          {DiagnosticSeverity::error,
           "MPF0005",
           "invalid MIR at '" + std::string(stage) +
               "': validator source binding inventory is missing, foreign, or inconsistent",
           {statement.line, 1}});
    }
  }
}

}  // namespace mpf::detail::mir
