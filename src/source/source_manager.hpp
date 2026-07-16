#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "source_text.hpp"

namespace mpf::detail {

using SourceId = std::size_t;
inline constexpr SourceId invalid_source_id = static_cast<SourceId>(-1);

class SourceManager final {
 public:
  [[nodiscard]] SourceId add(std::string_view content, std::string filename = {});
  [[nodiscard]] const SourceText& source(SourceId id) const;
  [[nodiscard]] std::optional<SourceId> find(std::string_view filename) const;
  [[nodiscard]] std::size_t size() const noexcept { return sources_.size(); }

 private:
  std::vector<std::unique_ptr<SourceText>> sources_;
  std::unordered_map<std::string, SourceId> filenames_;
};

}  // namespace mpf::detail
