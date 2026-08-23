#include "validator.hpp"

#include <string>
#include <utility>

namespace mpf::detail {
namespace {

void add_error(std::vector<Diagnostic>& diagnostics, const std::size_t line, std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF2061", std::move(message), {line, 1}});
}

bool javascript_boundary_conversion_representable(const mir::Program& program,
                                                  const mir::CallSite::Argument& argument) {
  if (!valid_argument_call_boundary(argument.boundary)) return false;
  if (argument.boundary.conversion == ArgumentBoundaryConversion::none) return true;
  if (!argument.type.valid() || !argument.shape.valid() || !argument.validated_type.valid() ||
      !argument.validated_shape.valid()) {
    return false;
  }
  const auto actual = mir::value_type(program, argument.type);
  const auto scalar =
      actual == ValueType::list ? mir::element_type(program, argument.type) : actual;
  const auto scalar_numeric =
      scalar == ValueType::integer || scalar == ValueType::real || scalar == ValueType::boolean;
  const auto validated = mir::value_type(program, argument.validated_type);
  if (has_argument_boundary_conversion(argument.boundary.conversion,
                                       ArgumentBoundaryConversion::matlab_class)) {
    switch (argument.boundary.class_constraint) {
      case ArgumentClassConstraint::matlab_double:
      case ArgumentClassConstraint::matlab_logical:
        if (!scalar_numeric) return false;
        break;
      case ArgumentClassConstraint::matlab_char:
      case ArgumentClassConstraint::none: return false;
    }
  }
  if (has_argument_boundary_conversion(argument.boundary.conversion,
                                       ArgumentBoundaryConversion::matlab_size)) {
    if (actual != ValueType::list && !scalar_numeric) return false;
    if ((argument.boundary.validated_rank == 0U) != (validated != ValueType::list)) return false;
  }
  return true;
}

}  // namespace

std::vector<Diagnostic> validate_javascript_capabilities(
    const mir::Program& program, const mir::AliasEffectTable& alias_effects) {
  auto diagnostics =
      mir::alias_effects_current(program, alias_effects)
          ? std::vector<Diagnostic>{}
          : mir::verify_alias_effects(program, alias_effects, "javascript-capabilities");
  if (!diagnostics.empty()) return diagnostics;
  for (const auto& call : program.calls) {
    const auto* instruction =
        call.instruction.valid() && call.instruction.value() < program.instructions.size()
            ? &program.instructions[call.instruction.value()]
            : nullptr;
    const auto line = instruction == nullptr ? 1U : instruction->location.line;
    for (const auto& argument : call.arguments) {
      if (!javascript_boundary_conversion_representable(program, argument)) {
        add_error(diagnostics, line,
                  "JavaScript cannot yet preserve this Matlab class conversion at a function "
                  "boundary");
      }
    }
  }
  return diagnostics;
}

}  // namespace mpf::detail
