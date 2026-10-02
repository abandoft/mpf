#pragma once

#include "mir.hpp"

namespace mpf::detail::mir {

struct ArgumentExitBinding {
  StorageId storage{};
};

struct ArgumentExitContext {
  Program& program;
  Function& function;
  const Statement& owner;
  std::vector<ArgumentExitBinding> outputs;
  std::vector<ArgumentExitBinding> inputs;
  TypeId literal_type{};
  ShapeId scalar_shape{};
  TypeId tuple_type{};
  TypeId workspace_type{};
  ShapeId workspace_shape{};
  IrIdAllocator<InstructionId>& instructions;
  IrIdAllocator<ValueId>& values;
  IrIdAllocator<BlockId>& blocks;
};

void lower_argument_exit(ArgumentExitContext context);
void verify_argument_outputs(const Program& program, std::vector<Diagnostic>& diagnostics,
                             std::string_view stage);

}  // namespace mpf::detail::mir
