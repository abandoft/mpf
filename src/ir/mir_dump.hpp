#pragma once

#include <string>

#include "memory_dependence.hpp"

namespace mpf::detail {

[[nodiscard]] std::string dump_mir(const mir::Program& program);
[[nodiscard]] std::string dump_mir(const mir::Program& program,
                                   const mir::AliasEffectTable& analysis);
[[nodiscard]] std::string dump_mir(const mir::Program& program,
                                   const mir::AliasEffectTable& alias_effects,
                                   const mir::MemoryDependenceTable& memory_dependences);

}  // namespace mpf::detail
