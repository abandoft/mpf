#include <algorithm>
#include <utility>

#include "compiler/array_storage.hpp"
#include "frontends/common/registry.hpp"
#include "ir/mir.hpp"
#include "ir/semantics.hpp"
#include "semantic/analyzer.hpp"
#include "source/source_manager.hpp"
#include "test_framework.hpp"

namespace {

using Storage = mpf::detail::ArrayStorageFormat;
using Policy = mpf::detail::semantic::ReductionStoragePolicy;

bool valid(const Policy policy, const Storage input, const Storage result,
           const bool scalar) noexcept {
  return mpf::detail::semantic::valid_logical_reduction_storage_contract(policy, input, result,
                                                                         scalar);
}

}  // namespace

TEST_CASE("logical reduction storage policy distinguishes dense sparse and scalar results") {
  REQUIRE(valid(Policy::dense, Storage::dense, Storage::dense, false));
  REQUIRE(valid(Policy::preserve_sparse, Storage::sparse_csc, Storage::sparse_csc, false));
  REQUIRE(valid(Policy::scalar_full, Storage::dense, Storage::none, true));
  REQUIRE(valid(Policy::scalar_full, Storage::sparse_csc, Storage::none, true));
  REQUIRE(valid(Policy::scalar_full, Storage::unknown, Storage::none, true));
  REQUIRE(valid(Policy::scalar_full, Storage::none, Storage::none, true));
}

TEST_CASE("logical reduction storage policy rejects implicit representation changes") {
  REQUIRE(!valid(Policy::none, Storage::dense, Storage::dense, false));
  REQUIRE(!valid(Policy::dense, Storage::sparse_csc, Storage::dense, false));
  REQUIRE(!valid(Policy::preserve_sparse, Storage::dense, Storage::sparse_csc, false));
  REQUIRE(!valid(Policy::preserve_sparse, Storage::sparse_csc, Storage::dense, false));
  REQUIRE(!valid(Policy::scalar_full, Storage::sparse_csc, Storage::sparse_csc, true));
  REQUIRE(!valid(Policy::scalar_full, Storage::dense, Storage::none, false));
}

TEST_CASE("Matlab Analyzer preserves sparse storage for nonscalar logical reductions") {
  mpf::detail::SourceManager sources;
  const auto source_id = sources.add(
      "numeric = sparse([1 0; 0 2]);\n"
      "logical_values = sparse([true false; true true]);\n"
      "empty = sparse([], [], [], 0, 3);\n"
      "by_column = any(numeric);\n"
      "by_row = all(logical_values, 2);\n"
      "empty_columns = all(empty);\n"
      "total = all(numeric, 'all');\n",
      "sparse_reduction_contract.m");
  const auto& frontend = mpf::detail::matlab_frontend();
  auto parsed = mpf::detail::parse_with_frontend(frontend, sources.source(source_id));
  REQUIRE(parsed.diagnostics.empty());
  auto lowered = frontend.lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  auto analysis = mpf::detail::analyze_program(lowered.program, std::move(lowered.semantics));
  REQUIRE(analysis.empty());

  std::size_t sparse_results = 0U;
  std::size_t scalar_results = 0U;
  for (const auto& facts : analysis.semantics.expressions) {
    const auto& reduction = facts.reduction;
    if (!reduction.valid()) continue;
    if (reduction.storage_policy == Policy::preserve_sparse) {
      ++sparse_results;
      REQUIRE(reduction.input_storage == Storage::sparse_csc);
      REQUIRE(reduction.result_storage == Storage::sparse_csc);
      REQUIRE(!reduction.scalar_result);
      REQUIRE(facts.array_storage == Storage::sparse_csc);
    } else if (reduction.storage_policy == Policy::scalar_full) {
      ++scalar_results;
      REQUIRE(reduction.input_storage == Storage::sparse_csc);
      REQUIRE(reduction.result_storage == Storage::none);
      REQUIRE(reduction.scalar_result);
      REQUIRE(facts.array_storage == Storage::none);
    }
  }
  REQUIRE(sparse_results == 3U);
  REQUIRE(scalar_results == 1U);

  auto mir = mpf::detail::mir::lower_from_hir(std::move(lowered.program),
                                              std::move(analysis.semantics), analysis.names);
  REQUIRE(mir.diagnostics.empty());
  REQUIRE(mpf::detail::mir::verify(mir.program, "sparse-reduction-storage").empty());

  const auto sparse_plan =
      std::find_if(mir.program.attributes.expressions.begin() + 1,
                   mir.program.attributes.expressions.end(), [](const auto& attributes) {
                     return attributes.reduction.storage_policy == Policy::preserve_sparse;
                   });
  REQUIRE(sparse_plan != mir.program.attributes.expressions.end());
  REQUIRE(sparse_plan->reduction.input_storage == Storage::sparse_csc);
  REQUIRE(sparse_plan->reduction.result_storage == Storage::sparse_csc);

  auto corrupted = mir.program;
  const auto corrupted_plan =
      std::find_if(corrupted.attributes.expressions.begin() + 1,
                   corrupted.attributes.expressions.end(), [](const auto& attributes) {
                     return attributes.reduction.storage_policy == Policy::preserve_sparse;
                   });
  REQUIRE(corrupted_plan != corrupted.attributes.expressions.end());
  corrupted_plan->reduction.result_storage = Storage::dense;
  REQUIRE(!mpf::detail::mir::verify(corrupted, "sparse-reduction-storage-corruption").empty());
}
