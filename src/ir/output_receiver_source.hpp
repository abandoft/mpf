#pragma once

#include <cstddef>
#include <string>

#include "compiler/output_receiver.hpp"
#include "ids.hpp"
#include "opcode.hpp"

namespace mpf::detail::mir {

// Resident MIR operation identities captured at the target-lowering boundary.
struct OutputReceiverSource {
  HirNodeId owner{};
  std::size_t position{0U};
  OutputReceiverKind kind{OutputReceiverKind::invalid};
  SymbolId symbol{};
  InstructionId instruction{};
  Opcode opcode{Opcode::invalid};
  StorageId storage{};
  ValueId result{};
  ValueId argument{};
  std::string name;
  SourceLocation location{};
};

}  // namespace mpf::detail::mir
