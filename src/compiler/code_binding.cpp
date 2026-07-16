#include "code_binding.hpp"

namespace mpf::detail {
namespace {

template <typename Expression>
void validate_expression(const Expression& expression, const CodeBindingLookup lookup,
                         const std::string_view target_name, std::vector<Diagnostic>& diagnostics) {
  if (expression.binding == BindingKind::builtin && expression.intrinsic != IntrinsicId::none) {
    const auto* binding = lookup == nullptr ? nullptr : lookup(expression.intrinsic);
    if (binding == nullptr || binding->kind == CodeBindingKind::unavailable) {
      const auto* descriptor = intrinsic_descriptor(expression.intrinsic);
      diagnostics.push_back(
          {DiagnosticSeverity::error, "MPF0004",
           "target '" + std::string(target_name) + "' has no code binding for intrinsic '" +
               std::string(descriptor == nullptr ? "unknown" : descriptor->name) + "'",
           expression.location});
    }
  }
  for (const auto& child : expression.children) {
    validate_expression(child, lookup, target_name, diagnostics);
  }
}

template <typename Statement>
void validate_statements(const std::vector<Statement>& statements, const CodeBindingLookup lookup,
                         const std::string_view target_name, std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    if (statement.has_expression) {
      validate_expression(statement.expression, lookup, target_name, diagnostics);
    }
    if (statement.has_secondary_expression) {
      validate_expression(statement.secondary_expression, lookup, target_name, diagnostics);
    }
    if (statement.has_tertiary_expression) {
      validate_expression(statement.tertiary_expression, lookup, target_name, diagnostics);
    }
    if (statement.has_target_expression) {
      validate_expression(statement.target_expression, lookup, target_name, diagnostics);
    }
    for (const auto& parameter_default : statement.parameter_defaults) {
      if (parameter_default.valid()) {
        validate_expression(parameter_default, lookup, target_name, diagnostics);
      }
    }
    for (const auto& selector : statement.case_selectors) {
      if (selector.has_lower) validate_expression(selector.lower, lookup, target_name, diagnostics);
      if (selector.has_upper) validate_expression(selector.upper, lookup, target_name, diagnostics);
    }
    validate_statements(statement.body, lookup, target_name, diagnostics);
    validate_statements(statement.alternative, lookup, target_name, diagnostics);
  }
}

}  // namespace

std::vector<Diagnostic> validate_code_bindings(const Program& program,
                                               const CodeBindingLookup lookup,
                                               const std::string_view target_name) {
  std::vector<Diagnostic> diagnostics;
  validate_statements(program.statements, lookup, target_name, diagnostics);
  return diagnostics;
}

std::vector<Diagnostic> validate_code_bindings(const mir::Program& program,
                                               const CodeBindingLookup lookup,
                                               const std::string_view target_name) {
  std::vector<Diagnostic> diagnostics;
  validate_statements(program.statements, lookup, target_name, diagnostics);
  return diagnostics;
}

}  // namespace mpf::detail
