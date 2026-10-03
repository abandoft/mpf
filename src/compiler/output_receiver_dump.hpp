#pragma once

#include <iomanip>
#include <ostream>
#include <vector>

#include "output_receiver.hpp"

namespace mpf::detail {

inline void dump_output_receiver_list(std::ostream& output,
                                      const std::vector<OutputReceiver>& receivers) {
  output << '[';
  for (std::size_t index = 0U; index < receivers.size(); ++index) {
    if (index != 0U) output << ',';
    const auto& receiver = receivers[index];
    output << index << ':' << (receiver.binds() ? "binding" : "discard") << ':'
           << std::quoted(receiver.name) << '@' << receiver.location.line << ':'
           << receiver.location.column;
  }
  output << ']';
}

}  // namespace mpf::detail
