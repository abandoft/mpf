#pragma once

#include <cstddef>
#include <vector>

#include "compiler/argument_validation.hpp"
#include "ids.hpp"

namespace mpf::detail::mir {

// Raw incoming values are distinct from the initialized, normalized formal used by the body.
struct ArgumentEntryFlow {
  std::size_t parameter{0U};
  StorageId raw_storage{};
  StorageId storage{};
  ValueId selected{};
  InstructionId normalization{};
  InstructionId initialization{};
  ValueId result{};
  std::vector<InstructionId> validators;
  BlockId block{};
  BlockId continuation{};

  friend bool operator==(const ArgumentEntryFlow& left, const ArgumentEntryFlow& right) noexcept {
    return left.parameter == right.parameter && left.raw_storage == right.raw_storage &&
           left.storage == right.storage && left.selected == right.selected &&
           left.normalization == right.normalization &&
           left.initialization == right.initialization && left.result == right.result &&
           left.validators == right.validators && left.block == right.block &&
           left.continuation == right.continuation;
  }
};

// Minimal semantic projection used to legalize real resident entry operations, never an
// independently generated replacement for the MIR sequence.
struct ArgumentEntrySource {
  ArgumentEntryFlow flow;
  ArgumentClassConstraint class_constraint{ArgumentClassConstraint::none};
  bool dimensions_declared{false};
  std::vector<ArgumentDimensionConstraint> dimensions;
  std::size_t rank{0U};
  std::vector<ArgumentValidatorPlan> validators;

  friend bool operator==(const ArgumentEntrySource& left,
                         const ArgumentEntrySource& right) noexcept {
    return left.flow == right.flow && left.class_constraint == right.class_constraint &&
           left.dimensions_declared == right.dimensions_declared &&
           left.dimensions == right.dimensions && left.rank == right.rank &&
           left.validators == right.validators;
  }
};

}  // namespace mpf::detail::mir
