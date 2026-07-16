#pragma once

#include <string>

#include "mir.hpp"

namespace mpf::detail {

[[nodiscard]] std::string dump_mir(const mir::Program& program);
[[nodiscard]] std::string dump_mir(const mir::Program& program,
                                   const mir::AliasEffectTable& analysis);

}  // namespace mpf::detail
