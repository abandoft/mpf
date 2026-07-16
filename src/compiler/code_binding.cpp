#include "code_binding.hpp"

namespace mpf::detail {
namespace {

void validate_expression(const mir::Expression& expression,
                         const mir::ExpressionAttributes& attributes,
                         const CodeBindingLookup lookup, const std::string_view target_name,
                         std::vector<Diagnostic>& diagnostics) {
  if (attributes.binding == BindingKind::builtin && attributes.intrinsic != IntrinsicId::none) {
    const auto* binding = lookup == nullptr ? nullptr : lookup(attributes.intrinsic);
    if (binding == nullptr || binding->kind == CodeBindingKind::unavailable) {
      const auto* descriptor = intrinsic_descriptor(attributes.intrinsic);
      diagnostics.push_back(
          {DiagnosticSeverity::error, "MPF0004",
           "target '" + std::string(target_name) + "' has no code binding for intrinsic '" +
               std::string(descriptor == nullptr ? "unknown" : descriptor->name) + "'",
           expression.location});
    }
  }
}

}  // namespace

std::vector<Diagnostic> validate_code_bindings(const mir::Program& program,
                                               const CodeBindingLookup lookup,
                                               const std::string_view target_name) {
  std::vector<Diagnostic> diagnostics;
  for (std::size_t index = 1; index < program.expressions.size(); ++index) {
    const auto id = MirExpressionId{static_cast<MirExpressionId::value_type>(index)};
    const auto* expression_attributes = mir::attributes(program, id);
    if (expression_attributes != nullptr) {
      validate_expression(program.expressions[index], *expression_attributes, lookup, target_name,
                          diagnostics);
    }
  }
  return diagnostics;
}

}  // namespace mpf::detail
