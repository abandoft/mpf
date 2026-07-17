#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory_resource>
#include <new>
#include <string>
#include <vector>

#include "mpf/transpiler.hpp"
#include "source/source_manager.hpp"

namespace mpf::detail {

class CountingMemoryResource final : public std::pmr::memory_resource {
 public:
  explicit CountingMemoryResource(const std::size_t limit) : limit_(limit) {}

  [[nodiscard]] std::size_t current_bytes() const noexcept { return current_; }
  [[nodiscard]] std::size_t peak_bytes() const noexcept { return peak_; }

 private:
  void* do_allocate(const std::size_t bytes, const std::size_t alignment) override {
    if (bytes > limit_ - std::min(limit_, current_)) throw std::bad_alloc{};
    void* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    current_ += bytes;
    if (current_ > peak_) peak_ = current_;
    return result;
  }

  void do_deallocate(void* pointer, const std::size_t bytes, const std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    current_ = bytes > current_ ? 0 : current_ - bytes;
  }

  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }

  std::size_t limit_{0};
  std::size_t current_{0};
  std::size_t peak_{0};
};

struct StageMetric {
  std::string stage;
  std::size_t nodes{0};
  std::size_t duration_nanoseconds{0};
  std::size_t arena_bytes{0};
};

class CompilationSession final {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  explicit CompilationSession(ResourceLimits limits = {})
      : counting_(limits.max_arena_bytes),
        arena_(&counting_),
        limits_(limits),
        started_(Clock::now()) {}

  [[nodiscard]] SourceManager& sources() noexcept { return sources_; }
  [[nodiscard]] std::pmr::memory_resource* memory_resource() noexcept { return &arena_; }
  [[nodiscard]] const ResourceLimits& limits() const noexcept { return limits_; }
  [[nodiscard]] TimePoint begin_stage() const noexcept { return Clock::now(); }

  void record(std::string stage, const std::size_t nodes, const TimePoint started) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started);
    metrics_.push_back({std::move(stage), nodes, static_cast<std::size_t>(elapsed.count()),
                        counting_.peak_bytes()});
  }

  void record_duration(std::string stage, const std::size_t nodes,
                       const std::size_t duration_nanoseconds) {
    metrics_.push_back({std::move(stage), nodes, duration_nanoseconds, counting_.peak_bytes()});
  }

  [[nodiscard]] const std::vector<StageMetric>& metrics() const noexcept { return metrics_; }
  [[nodiscard]] std::size_t peak_arena_bytes() const noexcept { return counting_.peak_bytes(); }
  [[nodiscard]] std::size_t elapsed_nanoseconds() const noexcept {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started_);
    return static_cast<std::size_t>(elapsed.count());
  }

 private:
  CountingMemoryResource counting_;
  std::pmr::monotonic_buffer_resource arena_;
  SourceManager sources_;
  ResourceLimits limits_;
  TimePoint started_;
  std::vector<StageMetric> metrics_;
};

}  // namespace mpf::detail
