#include "compiler/array_storage.hpp"
#include "ir/semantics.hpp"
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
