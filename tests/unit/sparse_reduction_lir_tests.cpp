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

using Operation = mpf::detail::semantic::ReductionOperation;
using AxisPolicy = mpf::detail::semantic::ReductionAxisPolicy;
using StoragePolicy = mpf::detail::semantic::ReductionStoragePolicy;
using Storage = mpf::detail::ArrayStorageFormat;

template <typename Expression>
void configure_callee(Expression& callee, const Operation operation) {
  callee.kind = mpf::detail::ExpressionKind::identifier;
  callee.binding = mpf::detail::BindingKind::builtin;
  callee.intrinsic = operation == Operation::logical_all ? mpf::detail::IntrinsicId::logical_all
                                                         : mpf::detail::IntrinsicId::logical_any;
}

template <typename Expression>
void configure_operand(Expression& operand) {
  operand.kind = mpf::detail::ExpressionKind::identifier;
  operand.value = "sparse_value";
  operand.inferred_type = mpf::detail::ValueType::list;
  operand.element_type = mpf::detail::ValueType::real;
  operand.element_numeric_type = mpf::detail::real_numeric_type;
  operand.array_storage = Storage::sparse_csc;
  operand.shape = {2U, 3U};
}

template <typename Expression>
void configure_reduction(Expression& expression, const Operation operation,
                         const bool scalar_result) {
  expression.kind = mpf::detail::ExpressionKind::call;
  expression.inferred_type =
      scalar_result ? mpf::detail::ValueType::boolean : mpf::detail::ValueType::list;
  expression.element_type =
      scalar_result ? mpf::detail::ValueType::unknown : mpf::detail::ValueType::boolean;
  expression.numeric_type =
      scalar_result ? mpf::detail::logical_numeric_type : mpf::detail::no_numeric_type;
  expression.element_numeric_type =
      scalar_result ? mpf::detail::no_numeric_type : mpf::detail::logical_numeric_type;
  expression.array_storage = scalar_result ? Storage::none : Storage::sparse_csc;
  expression.shape = scalar_result ? std::vector<std::size_t>{} : std::vector<std::size_t>{1U, 3U};
  expression.reduction.operation = operation;
  expression.reduction.axis_policy =
      scalar_result ? AxisPolicy::all_dimensions : AxisPolicy::first_nonsingleton;
  expression.reduction.input_shape = {2U, 3U};
  expression.reduction.result_shape =
      scalar_result ? std::vector<std::size_t>{1U, 1U} : std::vector<std::size_t>{1U, 3U};
  expression.reduction.output_shape = expression.shape;
  expression.reduction.axes =
      scalar_result ? std::vector<std::size_t>{0U, 1U} : std::vector<std::size_t>{0U};
  expression.reduction.scalar_result = scalar_result;
  expression.reduction.storage_policy =
      scalar_result ? StoragePolicy::scalar_full : StoragePolicy::preserve_sparse;
  expression.reduction.input_storage = Storage::sparse_csc;
  expression.reduction.result_storage = scalar_result ? Storage::none : Storage::sparse_csc;
  expression.children.resize(scalar_result ? 3U : 2U);
  configure_callee(expression.children[0], operation);
  configure_operand(expression.children[1]);
  if (scalar_result) {
    expression.children[2].kind = mpf::detail::ExpressionKind::string_literal;
    expression.children[2].inferred_type = mpf::detail::ValueType::string;
    expression.children[2].value = "all";
  }
}

template <typename Program>
void configure_program(Program& program) {
  program.source_language = mpf::SourceLanguage::matlab;
  program.statements.resize(2U);
  configure_reduction(program.statements[0].expression, Operation::logical_all, false);
  configure_reduction(program.statements[1].expression, Operation::logical_any, true);
}

template <typename Program, typename Planner, typename Verifier>
void verify_sparse_reduction_plans(Program& program, Planner planner, Verifier verifier,
                                   const std::string& vector_token,
                                   const std::string& scalar_token) {
  planner(program);
  std::vector<mpf::Diagnostic> diagnostics;
  verifier(program, diagnostics);
  REQUIRE(diagnostics.empty());

  const auto& vector_plan = program.statements[0].expression.plan;
  const auto& scalar_plan = program.statements[1].expression.plan;
  REQUIRE(vector_plan.token == vector_token);
  REQUIRE(scalar_plan.token == scalar_token);
  REQUIRE((vector_plan.runtime_shape_arguments ==
           std::vector<std::vector<std::size_t>>{{2U, 3U}, {0U}, {1U, 3U}, {1U, 3U}}));
  REQUIRE((scalar_plan.runtime_shape_arguments ==
           std::vector<std::vector<std::size_t>>{{2U, 3U}, {0U, 1U}, {1U, 1U}, {}}));
  REQUIRE((vector_plan.runtime_integer_arguments ==
           std::vector<std::int64_t>{static_cast<std::int64_t>(Operation::logical_all),
                                     static_cast<std::int64_t>(StoragePolicy::preserve_sparse),
                                     static_cast<std::int64_t>(Storage::sparse_csc),
                                     static_cast<std::int64_t>(Storage::sparse_csc)}));
  REQUIRE((scalar_plan.runtime_integer_arguments ==
           std::vector<std::int64_t>{static_cast<std::int64_t>(Operation::logical_any),
                                     static_cast<std::int64_t>(StoragePolicy::scalar_full),
                                     static_cast<std::int64_t>(Storage::sparse_csc),
                                     static_cast<std::int64_t>(Storage::none)}));

  std::ostringstream dump;
  mpf::detail::dump_target_lir_body(dump, program, "test");
  REQUIRE(dump.str().find("storage-policy 2 storage 3->3") != std::string::npos);
  REQUIRE(dump.str().find("storage-policy 3 storage 3->0") != std::string::npos);

  program.statements[0].expression.plan.runtime_integer_arguments.back() =
      static_cast<std::int64_t>(Storage::dense);
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());

  diagnostics.clear();
  planner(program);
  program.statements[1].expression.reduction.input_storage = Storage::dense;
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());
}

}  // namespace

TEST_CASE("JavaScript LIR serializes sparse logical reduction target plans") {
  mpf::detail::javascript::lir::SemanticProgram program;
  configure_program(program);
  verify_sparse_reduction_plans(program, mpf::detail::javascript::plan_lir_representation,
                                mpf::detail::javascript::verify_lir_representation,
                                "__mpf_sparse_logical_reduce", "__mpf_sparse_logical_reduce");
}

TEST_CASE("C++ LIR serializes sparse logical reduction target plans") {
  mpf::detail::cpp::lir::SemanticProgram program;
  configure_program(program);
  verify_sparse_reduction_plans(program, mpf::detail::cpp::plan_lir_representation,
                                mpf::detail::cpp::verify_lir_representation,
                                "mpf_runtime::sparse_logical_reduce<true, 2>",
                                "mpf_runtime::sparse_logical_reduce<false, 0>");
}
