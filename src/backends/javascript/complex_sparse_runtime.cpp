#include "complex_sparse_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_javascript_complex_sparse_runtime(std::ostream& output) {
  output << R"MPF(function __mpf_sparse_ctranspose(value) {
  return __mpf_sparse_transpose_impl(value, true);
}
)MPF";
}

}  // namespace mpf::detail
