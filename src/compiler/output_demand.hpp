#pragma once

#include <cstddef>
#include <cstdint>

namespace mpf::detail {

enum class OutputDemandForm : std::uint8_t { none, expression, statement, prefix, validation };

// Source invocation demand is independent of the selected MIR value's arity. In particular,
// a Matlab statement requests zero outputs even when its first result can update ans.
struct SourceOutputDemand {
  std::size_t count{0U};
  OutputDemandForm form{OutputDemandForm::none};
  bool implicit_result{false};

  constexpr SourceOutputDemand() noexcept = default;
  constexpr SourceOutputDemand(const OutputDemandForm demand_form, const std::size_t requested,
                               const bool capture) noexcept
      : count(requested), form(demand_form), implicit_result(capture) {}

  [[nodiscard]] constexpr bool active() const noexcept { return form != OutputDemandForm::none; }

  [[nodiscard]] constexpr bool valid() const noexcept {
    switch (form) {
      case OutputDemandForm::none: return count == 0U && !implicit_result;
      case OutputDemandForm::expression: return count == 1U && !implicit_result;
      case OutputDemandForm::statement: return count == 0U;
      case OutputDemandForm::prefix: return count > 0U && !implicit_result;
      case OutputDemandForm::validation: return count == 0U && !implicit_result;
    }
    return false;
  }

  friend constexpr bool operator==(const SourceOutputDemand& left,
                                   const SourceOutputDemand& right) noexcept {
    return left.form == right.form && left.count == right.count &&
           left.implicit_result == right.implicit_result;
  }
  friend constexpr bool operator!=(const SourceOutputDemand& left,
                                   const SourceOutputDemand& right) noexcept {
    return !(left == right);
  }
};

static_assert(sizeof(SourceOutputDemand) <= 2U * sizeof(std::size_t));

}  // namespace mpf::detail
