#pragma once

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "ir/argument_exit_flow.hpp"
#include "ir/hir.hpp"

namespace mpf::detail {

// Source provenance only: each target independently chooses control and materialization forms.
template <typename Statement>
bool valid_argument_output_sources(const Statement& statement, const SourceLanguage language) {
  const auto empty_return = [&] {
    return statement.source_argument_return == mir::ArgumentReturnSource{} &&
           !statement.source_argument_return_exit.valid();
  };
  if (statement.kind != StatementKind::function) {
    if (!(statement.source_argument_exit == mir::ArgumentExitFlow{}) ||
        !statement.source_argument_outputs.empty())
      return false;
    if (statement.kind != StatementKind::return_statement ||
        !statement.source_argument_return_exit.valid())
      return empty_return();
    return language == SourceLanguage::matlab && !statement.has_expression &&
           statement.source_argument_return.origin == statement.origin &&
           statement.source_argument_return.block.valid() &&
           !statement.source_argument_return.implicit;
  }
  if (!empty_return()) return false;
  std::vector<const ArgumentValidationPlan*> declarations;
  for (const auto& plan : statement.argument_validations)
    if (plan.direction == ArgumentDirection::output) declarations.push_back(&plan);
  const auto& exit = statement.source_argument_exit;
  if (declarations.empty()) {
    if (!(exit == mir::ArgumentExitFlow{}) || !statement.source_argument_outputs.empty())
      return false;
  } else {
    if (language != SourceLanguage::matlab || !exit.owner.valid() ||
        exit.origin != statement.origin || !exit.merge.valid() || !exit.continuation.valid() ||
        !exit.returned.valid() || exit.returns.empty() ||
        declarations.size() != statement.source_argument_outputs.size() ||
        declarations.size() != statement.return_names.size())
      return false;
    for (std::size_t index = 0U; index < declarations.size(); ++index) {
      const auto& plan = *declarations[index];
      const auto& source = statement.source_argument_outputs[index];
      const auto& flow = source.flow;
      if (flow.output != plan.ordinal || !flow.source_storage.valid() || !flow.storage.valid() ||
          flow.source_storage == flow.storage || !flow.selection.valid() ||
          !flow.selected.valid() || !flow.normalization.valid() || !flow.initialization.valid() ||
          !flow.result.valid() || !flow.block.valid() || !flow.continuation.valid() ||
          flow.block == flow.continuation ||
          flow.block != (index == 0U
                             ? exit.merge
                             : statement.source_argument_outputs[index - 1U].flow.continuation) ||
          source.class_constraint != plan.class_constraint ||
          source.dimensions_declared != plan.dimensions_declared ||
          source.dimensions != plan.dimensions || source.rank != plan.validated_rank ||
          source.validators != plan.validators ||
          flow.validators.size() != source.validators.size() ||
          std::any_of(flow.validators.begin(), flow.validators.end(),
                      [](const auto id) { return !id.valid(); }))
        return false;
    }
    if (statement.source_argument_outputs.back().flow.continuation != exit.continuation ||
        (declarations.size() == 1U
             ? exit.aggregation.valid() ||
                   exit.returned != statement.source_argument_outputs.front().flow.result
             : !exit.aggregation.valid()))
      return false;
  }
  std::unordered_map<HirNodeId, const mir::ArgumentReturnSource*> returns;
  std::size_t implicit = 0U;
  for (const auto& source : exit.returns) {
    if (!source.origin.valid() || !source.block.valid() || source.block == exit.merge) return false;
    if (source.implicit) {
      if (++implicit > 1U || source.origin != statement.origin) return false;
    } else if (!returns.emplace(source.origin, &source).second)
      return false;
  }
  std::vector<const Statement*> pending;
  pending.reserve(statement.body.size() + statement.alternative.size());
  for (const auto& child : statement.body) pending.push_back(&child);
  for (const auto& child : statement.alternative) pending.push_back(&child);
  for (std::size_t index = 0U; index < pending.size(); ++index) {
    const auto& child = *pending[index];
    if (child.kind == StatementKind::function) continue;
    if (child.kind == StatementKind::return_statement) {
      const auto found = returns.find(child.origin);
      if (declarations.empty()) {
        if (!(child.source_argument_return == mir::ArgumentReturnSource{}) ||
            child.source_argument_return_exit.valid())
          return false;
      } else {
        if (found == returns.end() || !(child.source_argument_return == *found->second) ||
            child.source_argument_return_exit != exit.merge || child.has_expression)
          return false;
        returns.erase(found);
      }
    }
    for (const auto& nested : child.body) pending.push_back(&nested);
    for (const auto& nested : child.alternative) pending.push_back(&nested);
  }
  return returns.empty();
}

}  // namespace mpf::detail
