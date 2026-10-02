#pragma once

#include <cstddef>
#include <vector>

#include "ids.hpp"

namespace mpf::detail::mir {

// A small, target-neutral provenance record projected from a verified executable flow.
struct ParameterDefaultSource {
  std::size_t parameter{0U};
  HirNodeId source{};
  StorageId storage{};
  InstructionId presence{};
  InstructionId initialization{};
  BlockId merge_block{};
  ValueId result{};
};

// Default expression instructions are resident only in default_blocks. Both paths publish
// a typed formal storage version through the merge block before subsequent defaults/body.
struct ParameterDefaultFlow : ParameterDefaultSource {
  BlockId test_block{};
  BlockId present_block{};
  std::vector<BlockId> default_blocks;
  BlockId default_exit{};
};

}  // namespace mpf::detail::mir
