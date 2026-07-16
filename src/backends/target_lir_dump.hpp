#pragma once

#include <iomanip>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace mpf::detail {

template <typename Expression>
void dump_target_expression(std::ostream& output, const Expression& expression,
                            const std::size_t depth) {
  if (!expression.valid()) return;
  output << std::string(depth * 2U, ' ') << "expr %l" << expression.id.value() << " origin %h"
         << expression.origin.value() << " kind " << static_cast<int>(expression.kind) << " type "
         << static_cast<int>(expression.inferred_type) << " binding "
         << static_cast<int>(expression.binding) << " intrinsic "
         << static_cast<int>(expression.intrinsic) << " shape [";
  for (std::size_t index = 0; index < expression.shape.size(); ++index) {
    if (index != 0) output << ',';
    output << expression.shape[index];
  }
  output << "] value " << std::quoted(expression.value);
  if (!expression.argument_transfers.empty()) {
    output << " transfers [";
    for (std::size_t index = 0; index < expression.argument_transfers.size(); ++index) {
      if (index != 0) output << ',';
      output << static_cast<int>(expression.argument_transfers[index]);
    }
    output << ']';
  }
  output << '\n';
  for (const auto& child : expression.children) {
    dump_target_expression(output, child, depth + 1U);
  }
}

template <typename Statement>
void dump_target_statements(std::ostream& output, const std::vector<Statement>& statements,
                            const std::size_t depth) {
  for (const auto& statement : statements) {
    output << std::string(depth * 2U, ' ') << "stmt %l" << statement.id.value() << " origin %h"
           << statement.origin.value() << " kind " << static_cast<int>(statement.kind) << " line "
           << statement.line << " name " << std::quoted(statement.name) << '\n';
    dump_target_expression(output, statement.expression, depth + 1U);
    dump_target_expression(output, statement.secondary_expression, depth + 1U);
    dump_target_expression(output, statement.tertiary_expression, depth + 1U);
    dump_target_expression(output, statement.target_expression, depth + 1U);
    for (const auto& expression : statement.parameter_defaults) {
      dump_target_expression(output, expression, depth + 1U);
    }
    for (const auto& selector : statement.case_selectors) {
      dump_target_expression(output, selector.lower, depth + 1U);
      dump_target_expression(output, selector.upper, depth + 1U);
    }
    dump_target_statements(output, statement.body, depth + 1U);
    dump_target_statements(output, statement.alternative, depth + 1U);
  }
}

template <typename Program>
void dump_target_lir_body(std::ostream& output, const Program& program,
                          const std::string_view target) {
  output << target << "-semantic-lir-v1 revision " << program.revision << " nodes "
         << program.node_count << " runtime 0x" << std::hex << program.runtime.bits << std::dec
         << '\n';
  output << "dependencies";
  for (const auto dependency : program.dependencies) output << ' ' << dependency;
  output << '\n';
  dump_target_statements(output, program.statements, 0);
}

}  // namespace mpf::detail
