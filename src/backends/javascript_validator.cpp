#include "javascript_validator.hpp"

namespace mpf::detail {

std::vector<Diagnostic> validate_javascript_capabilities(
    const mir::Program& program, const mir::AliasEffectTable& alias_effects) {
  return mir::alias_effects_current(program, alias_effects)
             ? std::vector<Diagnostic>{}
             : mir::verify_alias_effects(program, alias_effects, "javascript-capabilities");
}

}  // namespace mpf::detail
