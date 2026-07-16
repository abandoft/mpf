#pragma once

namespace mpf::detail::semantic {

enum class Truthiness { native, dynamic };
enum class LogicalResult { boolean, operand };
enum class Equality { native, structural };
enum class Division { native, real_quotient };
enum class IndexLayout { row_major, column_major };
enum class TopLevelStorage { module, entry_function };

struct Profile {
  Truthiness truthiness{Truthiness::native};
  LogicalResult logical_result{LogicalResult::boolean};
  Equality equality{Equality::native};
  Division division{Division::native};
  IndexLayout layout{IndexLayout::row_major};
  TopLevelStorage top_level_storage{TopLevelStorage::module};
  bool resizable_sections{false};
  bool emit_parameter_defaults{false};
};

}  // namespace mpf::detail::semantic
