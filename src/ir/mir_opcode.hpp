#pragma once

#include "mir.hpp"

namespace mpf::detail::mir {

[[nodiscard]] inline Opcode expression_opcode(
    const ExpressionKind kind, const BindingKind binding = BindingKind::unresolved) noexcept {
  switch (kind) {
    case ExpressionKind::number_literal:
    case ExpressionKind::string_literal:
    case ExpressionKind::boolean_literal:
    case ExpressionKind::null_literal:
    case ExpressionKind::omitted_argument: return Opcode::literal;
    case ExpressionKind::identifier:
      return binding == BindingKind::variable ? Opcode::load : Opcode::identifier;
    case ExpressionKind::unary: return Opcode::unary;
    case ExpressionKind::binary: return Opcode::binary;
    case ExpressionKind::comparison_chain: return Opcode::comparison_chain;
    case ExpressionKind::conditional: return Opcode::conditional;
    case ExpressionKind::call: return Opcode::call;
    case ExpressionKind::member: return Opcode::member;
    case ExpressionKind::index: return Opcode::index;
    case ExpressionKind::slice: return Opcode::slice;
    case ExpressionKind::list:
    case ExpressionKind::tuple: return Opcode::aggregate;
    case ExpressionKind::invalid: return Opcode::invalid;
  }
  return Opcode::invalid;
}

[[nodiscard]] inline Opcode statement_opcode(const StatementKind kind,
                                             const bool has_expression = false) noexcept {
  switch (kind) {
    case StatementKind::declaration: return has_expression ? Opcode::store : Opcode::allocate;
    case StatementKind::assignment:
    case StatementKind::multi_assignment: return Opcode::store;
    case StatementKind::indexed_assignment: return Opcode::store_indexed;
    case StatementKind::print: return Opcode::output;
    case StatementKind::return_statement: return Opcode::return_value;
    case StatementKind::expression: return Opcode::expression;
    case StatementKind::if_statement:
    case StatementKind::select_case:
    case StatementKind::case_clause: return Opcode::selection;
    case StatementKind::while_loop:
    case StatementKind::range_loop: return Opcode::loop;
    case StatementKind::for_loop: return Opcode::store;
    case StatementKind::function: return Opcode::function;
    case StatementKind::break_statement:
    case StatementKind::continue_statement: return Opcode::control;
  }
  return Opcode::invalid;
}

}  // namespace mpf::detail::mir
