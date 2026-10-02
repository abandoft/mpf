#pragma once

#include <cstddef>
#include <vector>

#include "compiler/argument_validation.hpp"
#include "ids.hpp"

namespace mpf::detail::mir {

struct ArgumentReturnSource {
  HirNodeId origin{};
  BlockId block{};
  bool implicit{false};

  friend bool operator==(const ArgumentReturnSource& left,
                         const ArgumentReturnSource& right) noexcept {
    return left.origin == right.origin && left.block == right.block &&
           left.implicit == right.implicit;
  }
};

// Normal returns leave body exception regions before the shared boundary sequence executes.
struct ArgumentExitFlow {
  MirStatementId owner{};
  HirNodeId origin{};
  BlockId merge{};
  BlockId continuation{};
  InstructionId aggregation{};
  ValueId returned{};
  std::vector<ArgumentReturnSource> returns;

  friend bool operator==(const ArgumentExitFlow& left, const ArgumentExitFlow& right) noexcept {
    return left.owner == right.owner && left.origin == right.origin && left.merge == right.merge &&
           left.continuation == right.continuation && left.aggregation == right.aggregation &&
           left.returned == right.returned && left.returns == right.returns;
  }
};

struct ArgumentOutputFlow {
  std::size_t output{0U};
  StorageId source_storage{};
  StorageId storage{};
  ValueId selected{};
  InstructionId selection{};
  InstructionId normalization{};
  InstructionId initialization{};
  ValueId result{};
  std::vector<InstructionId> validators;
  BlockId block{};
  BlockId continuation{};

  friend bool operator==(const ArgumentOutputFlow& left, const ArgumentOutputFlow& right) noexcept {
    return left.output == right.output && left.source_storage == right.source_storage &&
           left.storage == right.storage && left.selected == right.selected &&
           left.selection == right.selection && left.normalization == right.normalization &&
           left.initialization == right.initialization && left.result == right.result &&
           left.validators == right.validators && left.block == right.block &&
           left.continuation == right.continuation;
  }
};

struct ArgumentOutputSource {
  ArgumentOutputFlow flow;
  ArgumentClassConstraint class_constraint{ArgumentClassConstraint::none};
  bool dimensions_declared{false};
  std::vector<ArgumentDimensionConstraint> dimensions;
  std::size_t rank{0U};
  std::vector<ArgumentValidatorPlan> validators;

  friend bool operator==(const ArgumentOutputSource& left,
                         const ArgumentOutputSource& right) noexcept {
    return left.flow == right.flow && left.class_constraint == right.class_constraint &&
           left.dimensions_declared == right.dimensions_declared &&
           left.dimensions == right.dimensions && left.rank == right.rank &&
           left.validators == right.validators;
  }
};

}  // namespace mpf::detail::mir
