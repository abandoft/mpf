#pragma once

#include <algorithm>
#include <any>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

#include "mpf/diagnostic.hpp"

namespace mpf::detail {

struct PassInstrumentation {
  std::string_view name;
  std::size_t diagnostics_before{0};
  std::size_t diagnostics_after{0};
  std::uint64_t revision_before{0};
  std::uint64_t revision_after{0};
  std::uint64_t elapsed_nanoseconds{0};
};

template <typename Ir>
class AnalysisManager final {
 public:
  template <typename Result, typename Compute>
  [[nodiscard]] const Result& get(const Ir& ir, const std::string_view name, Compute&& compute) {
    const auto type = std::type_index(typeid(Result));
    for (auto& entry : entries_) {
      if (entry.name == name && entry.type == type) {
        if (entry.revision != ir.revision) {
          entry.value = std::forward<Compute>(compute)(ir);
          entry.revision = ir.revision;
        }
        return std::any_cast<const Result&>(entry.value);
      }
    }
    entries_.push_back({std::string(name), type, ir.revision, std::forward<Compute>(compute)(ir)});
    return std::any_cast<const Result&>(entries_.back().value);
  }

  void invalidate(const std::uint64_t revision, const std::vector<std::string_view>& preserved) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const Entry& entry) {
                                    return std::find(preserved.begin(), preserved.end(),
                                                     entry.name) == preserved.end();
                                  }),
                   entries_.end());
    for (auto& entry : entries_) entry.revision = revision;
  }

  void clear() noexcept { entries_.clear(); }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

 private:
  struct Entry {
    std::string name;
    std::type_index type{typeid(void)};
    std::uint64_t revision{0};
    std::any value;
  };

  std::vector<Entry> entries_;
};

template <typename Ir>
using PassCallback = std::function<std::vector<Diagnostic>(Ir& ir)>;

template <typename Ir>
using VerifierCallback = std::vector<Diagnostic> (*)(const Ir& ir, std::string_view stage);

template <typename Ir>
using RevisionCallback = void (*)(Ir& ir, std::uint64_t revision);

template <typename Ir>
struct PassDescriptor {
  PassDescriptor(std::string_view pass_name, PassCallback<Ir> callback, bool mutating = true,
                 std::vector<std::string_view> preserved = {}, bool is_deterministic = true,
                 bool is_reentrant = true, RevisionCallback<Ir> revision_callback = nullptr)
      : name(pass_name),
        run(std::move(callback)),
        mutates(mutating),
        preserved_analyses(std::move(preserved)),
        deterministic(is_deterministic),
        reentrant(is_reentrant),
        synchronize_revision(revision_callback) {}

  std::string_view name;
  PassCallback<Ir> run{nullptr};
  bool mutates{true};
  std::vector<std::string_view> preserved_analyses;
  bool deterministic{true};
  bool reentrant{true};
  RevisionCallback<Ir> synchronize_revision{nullptr};
};

template <typename Ir>
class PassManager final {
 public:
  explicit PassManager(const VerifierCallback<Ir> verifier) : verifier_(verifier) {}

  void add(PassDescriptor<Ir> pass) { passes_.push_back(std::move(pass)); }

  [[nodiscard]] std::vector<Diagnostic> run(Ir& ir) {
    instrumentation_.clear();
    auto diagnostics =
        verifier_ == nullptr ? std::vector<Diagnostic>{} : verifier_(ir, "pipeline-input");
    if (has_error(diagnostics)) return diagnostics;
    for (const auto& pass : passes_) {
      const auto before = diagnostics.size();
      const auto revision_before = ir.revision;
      if (pass.name.empty() || !pass.run || !pass.deterministic) {
        diagnostics.push_back({DiagnosticSeverity::error,
                               "MPF0005",
                               "invalid pass descriptor in compiler pipeline",
                               {1, 1}});
        return diagnostics;
      }
      const auto started = std::chrono::steady_clock::now();
      auto pass_diagnostics = pass.run(ir);
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - started);
      const auto pass_failed = has_error(pass_diagnostics);
      diagnostics.insert(diagnostics.end(), std::make_move_iterator(pass_diagnostics.begin()),
                         std::make_move_iterator(pass_diagnostics.end()));
      if (pass.mutates) {
        ++ir.revision;
        if (pass.synchronize_revision != nullptr) {
          pass.synchronize_revision(ir, ir.revision);
        }
        analyses_.invalidate(ir.revision, pass.preserved_analyses);
      }
      instrumentation_.push_back({pass.name, before, diagnostics.size(), revision_before,
                                  ir.revision, static_cast<std::uint64_t>(elapsed.count())});
      if (pass_failed) return diagnostics;
      if (verifier_ != nullptr) {
        auto verifier_diagnostics = verifier_(ir, pass.name);
        const auto verifier_failed = has_error(verifier_diagnostics);
        diagnostics.insert(diagnostics.end(), std::make_move_iterator(verifier_diagnostics.begin()),
                           std::make_move_iterator(verifier_diagnostics.end()));
        if (verifier_failed) return diagnostics;
      }
    }
    return diagnostics;
  }

  [[nodiscard]] const std::vector<PassInstrumentation>& instrumentation() const noexcept {
    return instrumentation_;
  }

  [[nodiscard]] AnalysisManager<Ir>& analyses() noexcept { return analyses_; }
  [[nodiscard]] const AnalysisManager<Ir>& analyses() const noexcept { return analyses_; }

 private:
  static bool has_error(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
      return diagnostic.severity == DiagnosticSeverity::error;
    });
  }

  VerifierCallback<Ir> verifier_{nullptr};
  AnalysisManager<Ir> analyses_;
  std::vector<PassDescriptor<Ir>> passes_;
  std::vector<PassInstrumentation> instrumentation_;
};

}  // namespace mpf::detail
