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

using Axis = mpf::detail::semantic::BroadcastAxis;
using Operation = mpf::detail::semantic::SparseArithmeticOperation;
using Storage = mpf::detail::ArrayStorageFormat;
using StoragePolicy = mpf::detail::semantic::SparseArithmeticStoragePolicy;

constexpr std::size_t kRows = 2U;
constexpr std::size_t kColumns = 3U;

template <typename Expression>
void configure_operand(Expression& expression, const Storage storage, const char* name) {
  expression.kind = mpf::detail::ExpressionKind::identifier;
  expression.value = name;
  expression.inferred_type =
      storage == Storage::none ? mpf::detail::ValueType::real : mpf::detail::ValueType::list;
  expression.numeric_type =
      storage == Storage::none ? mpf::detail::real_numeric_type : mpf::detail::no_numeric_type;
  expression.element_type =
      storage == Storage::none ? mpf::detail::ValueType::unknown : mpf::detail::ValueType::real;
  expression.element_numeric_type =
      storage == Storage::none ? mpf::detail::no_numeric_type : mpf::detail::real_numeric_type;
  expression.array_storage = storage;
  if (storage != Storage::none) expression.shape = {kRows, kColumns};
}

template <typename Statement>
void configure_binary(Statement& statement, const Operation operation, const Storage left_storage,
                      const Storage right_storage, const Storage result_storage,
                      const StoragePolicy policy, std::vector<Axis> axes) {
  auto& expression = statement.expression;
  expression.kind = mpf::detail::ExpressionKind::binary;
  expression.value = operation == Operation::add ? "+" : "-";
  expression.operation = operation == Operation::add ? mpf::detail::BinaryOperator::add
                                                     : mpf::detail::BinaryOperator::subtract;
  expression.inferred_type = mpf::detail::ValueType::list;
  expression.element_type = mpf::detail::ValueType::real;
  expression.element_numeric_type = mpf::detail::real_numeric_type;
  expression.array_storage = result_storage;
  expression.shape = {kRows, kColumns};
  expression.array_operation = mpf::detail::semantic::ArrayOperation::matlab;
  const auto left_shape = left_storage == Storage::none ? std::vector<std::size_t>{}
                                                        : std::vector<std::size_t>{kRows, kColumns};
  const auto right_shape = right_storage == Storage::none
                               ? std::vector<std::size_t>{}
                               : std::vector<std::size_t>{kRows, kColumns};
  expression.sparse_arithmetic = {
      operation,      policy,        mpf::detail::semantic::BroadcastShapeSource::static_extents,
      left_storage,   right_storage, result_storage,
      left_shape,     right_shape,   {kRows, kColumns},
      std::move(axes)};
  expression.children.resize(2U);
  configure_operand(expression.children[0], left_storage, "left");
  configure_operand(expression.children[1], right_storage, "right");
}

template <typename Program>
void configure_program(Program& program) {
  program.source_language = mpf::SourceLanguage::matlab;
  program.statements.resize(3U);
  configure_binary(program.statements[0], Operation::add, Storage::sparse_csc, Storage::sparse_csc,
                   Storage::sparse_csc, StoragePolicy::preserve_sparse, {Axis::match, Axis::match});
  configure_binary(program.statements[1], Operation::subtract, Storage::sparse_csc, Storage::dense,
                   Storage::dense, StoragePolicy::materialize_dense, {Axis::match, Axis::match});
  configure_binary(program.statements[2], Operation::subtract, Storage::none, Storage::sparse_csc,
                   Storage::dense, StoragePolicy::materialize_dense,
                   {Axis::expand_left, Axis::expand_left});
}

template <typename Program, typename Planner, typename Verifier>
void verify_sparse_arithmetic_plans(Program& program, Planner planner, Verifier verifier,
                                    const std::string& helper_prefix) {
  planner(program);
  std::vector<mpf::Diagnostic> diagnostics;
  verifier(program, diagnostics);
  REQUIRE(diagnostics.empty());

  REQUIRE(program.statements[0].expression.plan.token == helper_prefix + "add");
  REQUIRE(program.statements[1].expression.plan.token == helper_prefix + "subtract");
  REQUIRE(program.statements[2].expression.plan.token == helper_prefix + "subtract");
  REQUIRE((program.statements[0].expression.plan.runtime_shape_arguments ==
           std::vector<std::vector<std::size_t>>{
               {kRows, kColumns}, {kRows, kColumns}, {kRows, kColumns}}));
  REQUIRE((program.statements[2].expression.plan.runtime_shape_arguments ==
           std::vector<std::vector<std::size_t>>{{}, {kRows, kColumns}, {kRows, kColumns}}));
  REQUIRE((program.statements[0].expression.plan.runtime_integer_arguments ==
           std::vector<std::int64_t>{static_cast<std::int64_t>(Operation::add),
                                     static_cast<std::int64_t>(StoragePolicy::preserve_sparse),
                                     static_cast<std::int64_t>(Storage::sparse_csc),
                                     static_cast<std::int64_t>(Storage::sparse_csc),
                                     static_cast<std::int64_t>(Storage::sparse_csc)}));
  REQUIRE((program.statements[2].expression.plan.runtime_integer_arguments ==
           std::vector<std::int64_t>{static_cast<std::int64_t>(Operation::subtract),
                                     static_cast<std::int64_t>(StoragePolicy::materialize_dense),
                                     static_cast<std::int64_t>(Storage::none),
                                     static_cast<std::int64_t>(Storage::sparse_csc),
                                     static_cast<std::int64_t>(Storage::dense)}));

  std::ostringstream dump;
  mpf::detail::dump_target_lir_body(dump, program, "test");
  REQUIRE(dump.str().find("sparse-arithmetic 1 storage-policy 1 storage 3,3->3") !=
          std::string::npos);
  REQUIRE(dump.str().find("sparse-arithmetic 2 storage-policy 2 storage 0,3->2") !=
          std::string::npos);

  program.statements[0].expression.plan.token = helper_prefix + "subtract";
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());

  diagnostics.clear();
  planner(program);
  program.statements[1].expression.plan.runtime_integer_arguments.back() =
      static_cast<std::int64_t>(Storage::sparse_csc);
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());

  diagnostics.clear();
  planner(program);
  program.statements[2].expression.plan.runtime_shape_arguments.front() = {1U, 1U};
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());

  diagnostics.clear();
  planner(program);
  program.statements[0].expression.sparse_arithmetic.storage_policy =
      StoragePolicy::materialize_dense;
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());
}

}  // namespace

TEST_CASE("JavaScript LIR serializes sparse arithmetic target plans") {
  mpf::detail::javascript::lir::SemanticProgram program;
  configure_program(program);
  verify_sparse_arithmetic_plans(program, mpf::detail::javascript::plan_lir_representation,
                                 mpf::detail::javascript::verify_lir_representation,
                                 "__mpf_sparse_");
}

TEST_CASE("C++ LIR serializes sparse arithmetic target plans") {
  mpf::detail::cpp::lir::SemanticProgram program;
  configure_program(program);
  verify_sparse_arithmetic_plans(program, mpf::detail::cpp::plan_lir_representation,
                                 mpf::detail::cpp::verify_lir_representation,
                                 "mpf_runtime::sparse_");
}
