#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "backends/common/lir_dump.hpp"
#include "backends/cpp/lir.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/lir.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "test_framework.hpp"

namespace {

using ExponentPolicy = mpf::detail::semantic::MatrixExponentPolicy;
using Operation = mpf::detail::semantic::MatrixOperation;
using Storage = mpf::detail::ArrayStorageFormat;
using StoragePolicy = mpf::detail::semantic::MatrixStoragePolicy;

constexpr std::size_t kOrder = 3U;

template <typename Program>
void configure_program(Program& program) {
  program.source_language = mpf::SourceLanguage::matlab;
  program.statements.resize(1U);
  auto& expression = program.statements.front().expression;
  expression.kind = mpf::detail::ExpressionKind::binary;
  expression.value = "^";
  expression.operation = mpf::detail::BinaryOperator::power;
  expression.inferred_type = mpf::detail::ValueType::list;
  expression.element_type = mpf::detail::ValueType::real;
  expression.element_numeric_type = mpf::detail::real_numeric_type;
  expression.array_storage = Storage::sparse_csc;
  expression.shape = {kOrder, kOrder};
  expression.array_operation = mpf::detail::semantic::ArrayOperation::matlab;
  expression.matrix_operation = {Operation::integer_power,
                                 mpf::detail::semantic::MatrixSolveKind::none,
                                 mpf::detail::semantic::MatrixNumericDomain::real,
                                 mpf::detail::semantic::MatrixConditionPolicy::none,
                                 mpf::detail::semantic::MatrixFactorizationPolicy::none,
                                 mpf::detail::semantic::MatrixStructurePolicy::none,
                                 StoragePolicy::sparse_csc_power,
                                 ExponentPolicy::nonnegative_safe_integer,
                                 Storage::sparse_csc,
                                 Storage::none,
                                 Storage::sparse_csc,
                                 {kOrder, kOrder},
                                 {},
                                 {kOrder, kOrder}};
  expression.children.resize(2U);
  auto& base = expression.children[0];
  base.kind = mpf::detail::ExpressionKind::identifier;
  base.value = "base";
  base.inferred_type = mpf::detail::ValueType::list;
  base.element_type = mpf::detail::ValueType::real;
  base.element_numeric_type = mpf::detail::real_numeric_type;
  base.array_storage = Storage::sparse_csc;
  base.shape = {kOrder, kOrder};
  auto& exponent = expression.children[1];
  exponent.kind = mpf::detail::ExpressionKind::identifier;
  exponent.value = "exponent";
  exponent.inferred_type = mpf::detail::ValueType::real;
  exponent.numeric_type = mpf::detail::real_numeric_type;
  exponent.array_storage = Storage::none;
}

template <typename Program, typename Planner, typename Verifier>
void verify_sparse_power_plan(Program& program, Planner planner, Verifier verifier,
                              const std::string& expected_helper) {
  planner(program);
  std::vector<mpf::Diagnostic> diagnostics;
  verifier(program, diagnostics);
  REQUIRE(diagnostics.empty());

  auto& expression = program.statements.front().expression;
  REQUIRE(expression.plan.token == expected_helper);
  REQUIRE((expression.plan.runtime_shape_arguments ==
           std::vector<std::vector<std::size_t>>{{kOrder, kOrder}, {kOrder, kOrder}}));
  REQUIRE((
      expression.plan.runtime_integer_arguments ==
      std::vector<std::int64_t>{static_cast<std::int64_t>(ExponentPolicy::nonnegative_safe_integer),
                                static_cast<std::int64_t>(Storage::sparse_csc),
                                static_cast<std::int64_t>(Storage::sparse_csc)}));

  std::ostringstream dump;
  mpf::detail::dump_target_lir_body(dump, program, "test");
  REQUIRE(dump.str().find(
              "matrix-operation 4 solve 0 numeric-domain 1 condition-policy 0 "
              "factorization-policy 0 structure-policy 0 storage-policy 5 exponent-policy 2 "
              "storage 3,0->3 [3,3]->[3,3]") != std::string::npos);
  REQUIRE(dump.str().find("runtime-shape-arguments [[3,3],[3,3]]") != std::string::npos);
  REQUIRE(dump.str().find("runtime-integer-arguments [2,3,3]") != std::string::npos);

  expression.plan.token += "_corrupt";
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());

  diagnostics.clear();
  planner(program);
  expression.plan.runtime_shape_arguments.back() = {kOrder, kOrder + 1U};
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());

  diagnostics.clear();
  planner(program);
  expression.plan.runtime_integer_arguments.front() =
      static_cast<std::int64_t>(ExponentPolicy::safe_integer);
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());

  diagnostics.clear();
  planner(program);
  expression.matrix_operation.exponent_policy = ExponentPolicy::safe_integer;
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());

  diagnostics.clear();
  expression.matrix_operation.exponent_policy = ExponentPolicy::nonnegative_safe_integer;
  expression.matrix_operation.result_storage = Storage::dense;
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());
}

}  // namespace

TEST_CASE("JavaScript LIR independently owns sparse matrix power plans") {
  mpf::detail::javascript::lir::SemanticProgram program;
  configure_program(program);
  verify_sparse_power_plan(program, mpf::detail::javascript::plan_lir_representation,
                           mpf::detail::javascript::verify_lir_representation,
                           "__mpf_sparse_mpower");
}

TEST_CASE("C++ LIR independently owns sparse matrix power plans") {
  mpf::detail::cpp::lir::SemanticProgram program;
  configure_program(program);
  verify_sparse_power_plan(program, mpf::detail::cpp::plan_lir_representation,
                           mpf::detail::cpp::verify_lir_representation,
                           "mpf_runtime::sparse_mpower");
}
