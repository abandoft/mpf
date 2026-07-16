#include "source_manager.hpp"

#include <stdexcept>
#include <utility>

namespace mpf::detail {

SourceId SourceManager::add(const std::string_view content, std::string filename) {
  const auto id = sources_.size();
  auto source = std::make_unique<SourceText>(content, std::move(filename));
  if (!source->filename().empty()) {
    filenames_.insert_or_assign(source->filename(), id);
  }
  sources_.push_back(std::move(source));
  return id;
}

const SourceText& SourceManager::source(const SourceId id) const {
  if (id >= sources_.size()) {
    throw std::out_of_range("MPF source id is not present in the source manager");
  }
  return *sources_[id];
}

std::optional<SourceId> SourceManager::find(const std::string_view filename) const {
  const auto found = filenames_.find(std::string(filename));
  if (found == filenames_.end()) return std::nullopt;
  return found->second;
}

}  // namespace mpf::detail
