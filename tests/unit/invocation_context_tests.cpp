#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "backends/common/identifier_mangler.hpp"
#include "backends/common/lir_builder.hpp"
#include "backends/common/lir_dump.hpp"
#include "backends/cpp/bindings.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/bindings.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "ir/dump.hpp"
#include "ir/mir_invocation_context.hpp"
#include "ir/mir_optimization.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

const std::string source =
    "checked();\nchecked\nvalue = checked();\n[first,second] = checked();\ndisp(checked());\n"
    "function [first,second] = checked(input)\narguments\n"
    "input (1,1) double = 7\nend\narguments (Output)\n"
    "first (1,1) double\nsecond (1,1) logical\nend\n"
    "first = input + nargout;\nsecond = nargout() > 0;\nend\n"
    "function output = other()\noutput = nargout;\nend\n";

void require_clean(const std::vector<mpf::Diagnostic>& diagnostics) {
  if (!diagnostics.empty()) throw std::runtime_error(diagnostics.front().message);
}

mir::Program lower(const std::string& text = source) {
  auto parsed = parse_with_frontend(matlab_frontend(), SourceText(text, "invocation.m"));
  REQUIRE(parsed.diagnostics.empty());
  auto hir = matlab_frontend().lower(std::move(parsed.ast));
  REQUIRE(hir.diagnostics.empty());
  auto analysis = analyze_program(hir.program, std::move(hir.semantics));
  REQUIRE(analysis.diagnostics.empty());
  REQUIRE(hir::verify_semantics(hir.program, analysis.semantics, "invocation").empty());
  auto result =
      mir::lower_from_hir(std::move(hir.program), std::move(analysis.semantics), analysis.names);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(mir::verify(result.program, "invocation").empty());
  return std::move(result.program);
}

mir::Function& function(mir::Program& program, const std::string_view name = "checked") {
  const auto found = std::find_if(program.functions.begin(), program.functions.end(),
                                  [&](const auto& value) { return value.name == name; });
  REQUIRE(found != program.functions.end());
  return *found;
}

mir::Instruction& query(mir::Program& program) {
  const auto found = std::find_if(
      program.instructions.begin(), program.instructions.end(),
      [](const auto& value) { return value.opcode == mir::Opcode::invocation_output_count; });
  REQUIRE(found != program.instructions.end());
  return *found;
}

template <typename Program>
auto& target_function(Program& program) {
  const auto found = std::find_if(program.statements.begin(), program.statements.end(),
                                  [](const auto& value) { return value.name == "checked"; });
  REQUIRE(found != program.statements.end());
  return *found;
}

javascript::lir::SemanticProgram javascript_plan(const mir::Program& program) {
  auto result = lower_structured_lir<javascript::lir::SemanticProgram, javascript::lir::Statement,
                                     javascript::lir::Expression, javascript::lir::CaseSelector>(
      program, [](HirNodeId, const IntrinsicId id) { return *javascript_code_binding(id); });
  result->runtime.require(javascript::lir::RuntimeFeature::argument_validation);
  result->runtime.require(javascript::lir::RuntimeFeature::arrays);
  result->runtime.require(javascript::lir::RuntimeFeature::complex_numbers);
  result->identifiers =
      allocate_identifiers(mpf::TargetLanguage::javascript, collect_identifier_inventory(*result));
  javascript::plan_lir_resources(*result, mpf::TranspileOptions{});
  javascript::plan_lir_representation(*result);
  std::vector<mpf::Diagnostic> diagnostics;
  javascript::verify_lir_resources(*result, diagnostics);
  javascript::verify_lir_representation(*result, diagnostics);
  require_clean(diagnostics);
  return std::move(*result);
}

cpp::lir::SemanticProgram cpp_plan(const mir::Program& program) {
  auto result = lower_structured_lir<cpp::lir::SemanticProgram, cpp::lir::Statement,
                                     cpp::lir::Expression, cpp::lir::CaseSelector>(
      program, [](HirNodeId, const IntrinsicId id) { return *cpp_code_binding(id); });
  result->runtime.require(cpp::lir::RuntimeFeature::argument_validation);
  result->identifiers =
      allocate_identifiers(mpf::TargetLanguage::cpp, collect_identifier_inventory(*result));
  cpp::plan_lir_resources(*result, mpf::TranspileOptions{});
  cpp::plan_lir_representation(*result);
  std::vector<mpf::Diagnostic> diagnostics;
  cpp::verify_lir_resources(*result, diagnostics);
  cpp::verify_lir_representation(*result, diagnostics);
  require_clean(diagnostics);
  return std::move(*result);
}

template <typename Program, typename Plan, typename Resources, typename Representation>
void reject_target_corruption(const Program& clean, Plan plan, Resources resources,
                              Representation representation) {
  for (const auto mutation : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
    auto program = clean;
    auto& owner = target_function(program);
    auto& invocation = owner.function_abi.invocation;
    if (mutation == 0) invocation.count_parameter = "foreign_count";
    if (mutation == 1) invocation.form = decltype(invocation.form)::none;
    if (mutation == 2) invocation.external_default_count = 0U;
    if (mutation == 3) program.statements.front().expression.plan.output_invocation.count = 9U;
    if (mutation == 4) {
      program.statements.front().expression.source_invocation.count = 9U;
      plan(program);
    }
    if (mutation == 5) {
      owner.body.front().expression.children.back().source_invocation_query = ValueId{999};
      plan(program);
    }
    if (mutation == 6) {
      owner.source_invocation_frame = {};
      plan(program);
    }
    if (mutation == 7) {
      program.statements.front().source_invocation_frame = owner.source_invocation_frame;
      plan(program);
    }
    if (mutation == 8) {
      owner.body.front().expression.children.back().source_invocation_query = {};
      plan(program);
    }
    std::vector<mpf::Diagnostic> diagnostics;
    resources(program, diagnostics);
    representation(program, diagnostics);
    REQUIRE(!diagnostics.empty());
  }
}

}  // namespace

TEST_CASE(
    "Matlab invocation formals do not pollute logical parameters defaults or signature types") {
  auto program = lower();
  const auto& owner = function(program);
  const auto& frame = owner.invocation_frame;
  REQUIRE(frame.active());
  REQUIRE(owner.parameter_types.size() == 1U);
  REQUIRE(owner.parameter_optional.size() == 1U);
  REQUIRE(owner.parameter_defaults.front().parameter == 0U);
  REQUIRE(program.types[owner.signature.value()].parameters.size() == 1U);
  const auto& arguments = program.blocks[owner.entry.value()].arguments;
  REQUIRE(arguments.size() == 2U);
  REQUIRE(arguments.back().value == frame.output_count);
  REQUIRE(arguments.back().type == frame.type);
  REQUIRE(arguments.back().shape == frame.shape);
  REQUIRE(!arguments.back().storage.valid());
  REQUIRE(mir::numeric_type(program, frame.type) == real_numeric_type);
}

TEST_CASE("Matlab nargout uses pure resident reads of each function's immutable invocation value") {
  auto program = lower();
  const auto effects = mir::analyze_alias_effects(program);
  std::size_t count = 0U;
  for (const auto& owner : program.functions) {
    for (const auto block : owner.blocks) {
      for (const auto instruction : program.blocks[block.value()].instructions) {
        const auto& value = program.instructions[instruction.value()];
        if (value.opcode != mir::Opcode::invocation_output_count) continue;
        ++count;
        REQUIRE(value.operands == std::vector<ValueId>{owner.invocation_frame.output_count});
        REQUIRE(value.intrinsic == IntrinsicId::matlab_nargout);
        REQUIRE(!value.storage.valid());
        REQUIRE(effects.instruction(instruction)->effects.bits() == 0U);
      }
    }
  }
  REQUIRE(count == 3U);
  REQUIRE(mir::verify_alias_effects(program, effects, "invocation-purity").empty());
}

TEST_CASE(
    "Matlab call immediates preserve zero expression and prefix demand independently of result "
    "arity") {
  const auto program = lower();
  const std::vector<std::size_t> expected{0U, 0U, 1U, 2U, 1U};
  REQUIRE(program.calls.size() == expected.size());
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    const auto& call = program.calls[index];
    REQUIRE(call.invocation_demand.active());
    REQUIRE(call.invocation_demand.count == expected[index]);
    REQUIRE(call.arguments.size() == 1U);
    REQUIRE(call.invocation_demand ==
            program.instructions[call.instruction.value()].invocation_demand);
    REQUIRE(call.invocation_demand.type ==
            program.functions[call.callee.value()].invocation_frame.type);
  }
  REQUIRE(program.calls.front().requested_results == 1U);
}

TEST_CASE("invocation verifier rejects missing foreign malformed and source-parameter frames") {
  const auto clean = lower();
  const std::vector<std::function<void(mir::Program&)>> mutations{
      [](auto& p) { function(p).invocation_frame = {}; },
      [](auto& p) { function(p).invocation_frame.type = {}; },
      [](auto& p) { function(p).invocation_frame.shape = {}; },
      [](auto& p) { function(p).invocation_frame.output_count = ValueId{999}; },
      [](auto& p) { p.blocks[function(p).entry.value()].arguments.back().storage = StorageId{1}; },
      [](auto& p) { p.blocks[function(p).entry.value()].arguments.pop_back(); },
      [](auto& p) { p.functions[1].invocation_frame = function(p).invocation_frame; },
      [](auto& p) { p.calls.front().invocation_demand.count = 2U; },
      [](auto& p) { p.instructions[p.calls.front().instruction.value()].invocation_demand = {}; },
      [](auto& p) { query(p).invocation_demand = p.calls.front().invocation_demand; }};
  for (const auto& mutate : mutations) {
    auto program = clean;
    mutate(program);
    std::vector<mpf::Diagnostic> diagnostics;
    mir::verify_invocation_contexts(program, diagnostics, "corrupt-invocation");
    REQUIRE(!diagnostics.empty());
    REQUIRE(!mir::verify(program, "corrupt-invocation-full").empty());
  }
}

TEST_CASE("nargout verifier rejects query rebinding opcode changes and foreign function values") {
  const auto clean = lower();
  for (const auto mutation : {0, 1, 2, 3, 4}) {
    auto program = clean;
    auto& value = query(program);
    if (mutation == 0) value.operands.clear();
    if (mutation == 1) value.operands = {function(program, "other").invocation_frame.output_count};
    if (mutation == 2) value.opcode = mir::Opcode::literal;
    if (mutation == 3) value.intrinsic = IntrinsicId::absolute;
    if (mutation == 4) value.storage = StorageId{1};
    std::vector<mpf::Diagnostic> diagnostics;
    mir::verify_invocation_contexts(program, diagnostics, "corrupt-query");
    REQUIRE(!diagnostics.empty());
  }
}

TEST_CASE("optimization remaps invocation shapes and retains default and call count provenance") {
  auto program = lower();
  const auto original = function(program).invocation_frame.shape;
  program.shapes.push_back(program.shapes[original.value()]);
  const ShapeId duplicate{static_cast<ShapeId::value_type>(program.shapes.size() - 1U)};
  for (auto& owner : program.functions) {
    if (!owner.invocation_frame.active()) continue;
    owner.invocation_frame.shape = duplicate;
    program.blocks[owner.entry.value()].arguments.back().shape = duplicate;
  }
  for (auto& call : program.calls) {
    call.invocation_demand.shape = duplicate;
    program.instructions[call.instruction.value()].invocation_demand.shape = duplicate;
  }
  REQUIRE(mir::verify(program, "duplicated-count-shape").empty());
  const auto result = mir::run_default_optimization_pipeline(program);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.statistics.canonicalized_shapes > 0U);
  REQUIRE(function(program).invocation_frame.shape == original);
  REQUIRE(mir::verify(program, "optimized-invocation").empty());
  REQUIRE(dump_mir(program).find("invocation-frame=%v") != std::string::npos);
}

TEST_CASE("JavaScript and cpp own independent trailing count ABI and collision-safe identities") {
  static_assert(!std::is_same_v<javascript::lir::InvocationAbi, cpp::lir::InvocationAbi>);
  const auto program = lower();
  auto javascript = javascript_plan(program);
  auto cpp = cpp_plan(program);
  const auto& js_owner = target_function(javascript);
  const auto& cpp_owner = target_function(cpp);
  REQUIRE(js_owner.function_abi.invocation.form ==
          javascript::lir::InvocationAbiForm::trailing_binary64);
  REQUIRE(cpp_owner.function_abi.invocation.form == cpp::lir::InvocationAbiForm::trailing_binary64);
  REQUIRE(!js_owner.function_abi.invocation.count_parameter.empty());
  REQUIRE(!cpp_owner.function_abi.invocation.count_parameter.empty());
  REQUIRE(js_owner.function_abi.parameters.size() == 1U);
  REQUIRE(cpp_owner.function_abi.parameters.size() == 1U);
  REQUIRE(javascript.statements.front().expression.plan.output_invocation.form ==
          javascript::lir::OutputInvocationForm::callee_count);
  REQUIRE(cpp.statements.front().expression.plan.output_invocation.form ==
          cpp::lir::OutputInvocationForm::callee_count);
  REQUIRE(js_owner.body.front().expression.children.back().plan.token ==
          js_owner.function_abi.invocation.count_parameter);
  REQUIRE(cpp_owner.body.front().expression.children.back().plan.token ==
          cpp_owner.function_abi.invocation.count_parameter);
}

TEST_CASE(
    "target verifiers reject damaged private count ABI and replanned corrupt invocation "
    "projections") {
  const auto program = lower();
  reject_target_corruption(javascript_plan(program), javascript::plan_lir_representation,
                           javascript::verify_lir_resources, javascript::verify_lir_representation);
  reject_target_corruption(cpp_plan(program), cpp::plan_lir_representation,
                           cpp::verify_lir_resources, cpp::verify_lir_representation);
}

TEST_CASE("Matlab nargout rejects script queries and unsupported function introspection") {
  for (const auto& text :
       {std::string{"disp(nargout);\n"}, std::string{"disp(nargout());\n"},
        std::string{"function output = query()\noutput = nargout('query');\nend\n"}}) {
    for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
      mpf::TranspileOptions options;
      options.language = mpf::SourceLanguage::matlab;
      options.target = target;
      const auto result = mpf::Transpiler{}.transpile(text, options);
      REQUIRE(!result.success());
      REQUIRE(result.code.empty());
      REQUIRE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                          [](const auto& diagnostic) { return diagnostic.code == "MPF2059"; }));
    }
  }
}

TEST_CASE("source variables parameters and local functions named nargout retain normal binding") {
  for (const auto& text :
       {std::string{"nargout = 42;\ndisp(nargout);\n"},
        std::string{
            "disp(identity(42));\nfunction output = identity(nargout)\noutput = nargout;\nend\n"},
        std::string{
            "disp(nargout(41));\nfunction output = nargout(value)\noutput = value + 1;\nend\n"}}) {
    for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
      mpf::TranspileOptions options;
      options.language = mpf::SourceLanguage::matlab;
      options.target = target;
      const auto result = mpf::Transpiler{}.transpile(text, options);
      REQUIRE(result.success());
    }
  }
}

TEST_CASE("Matlab arguments reject direct invocation queries before either target emits code") {
  const std::vector<std::string> invalid{
      "function output = query(input)\narguments\n"
      "input (1,1) double = nargout\nend\noutput = input;\nend\n",
      "disp(query(7));\nfunction output = query(input)\narguments\n"
      "input (1,1) double = nargout()\nend\noutput = input;\nend\n",
      "function output = query(input)\narguments\n"
      "input (1,1) double = 2 + nargout()\nend\noutput = input;\nend\n",
      "function output = query(input)\narguments\n"
      "input (1,1) double {mustBeGreaterThan(nargout)}\nend\noutput = input;\nend\n",
      "function output = query()\narguments (Output)\n"
      "output (1,1) double {mustBeGreaterThan(nargout())}\nend\noutput = 7;\nend\n"};
  for (const auto& text : invalid) {
    for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
      mpf::TranspileOptions options;
      options.language = mpf::SourceLanguage::matlab;
      options.target = target;
      const auto result = mpf::Transpiler{}.transpile(text, options);
      REQUIRE(!result.success());
      REQUIRE(result.code.empty());
      REQUIRE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                          [](const auto& diagnostic) {
                            return diagnostic.code == "MPF2059" &&
                                   diagnostic.message.find("arguments blocks") != std::string::npos;
                          }));
    }
  }
}

TEST_CASE("Matlab argument expressions distinguish bound variables and separate query workspaces") {
  for (const auto& text :
       {std::string{"disp(checked(7));\nfunction output = checked(nargout, input)\narguments\n"
                    "nargout (1,1) double\ninput (1,1) double = nargout\n"
                    "end\noutput = input;\nend\n"},
        std::string{"disp(checked());\nfunction output = checked(input)\narguments\n"
                    "input (1,1) double = helper()\nend\noutput = input + nargout;\nend\n"
                    "function output = helper()\noutput = nargout;\nend\n"}}) {
    const auto program = lower(text);
    REQUIRE(mir::verify(program, "argument-query-isolation").empty());
    for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
      mpf::TranspileOptions options;
      options.language = mpf::SourceLanguage::matlab;
      options.target = target;
      REQUIRE(mpf::Transpiler{}.transpile(text, options).success());
    }
  }
}

TEST_CASE("invocation count names avoid collisions with actual source formals in both targets") {
  const auto program = lower();
  auto javascript = javascript_plan(program);
  const auto& owner = target_function(javascript);
  const auto name = owner.function_abi.invocation.count_parameter;
  auto collision = source;
  std::size_t position = 0U;
  while ((position = collision.find("input", position)) != std::string::npos) {
    collision.replace(position, 5U, name);
    position += name.size();
  }
  const auto colliding = lower(collision);
  auto js = javascript_plan(colliding);
  auto cpp = cpp_plan(colliding);
  REQUIRE(target_function(js).parameters.front() == name);
  REQUIRE(target_function(cpp).parameters.front() == name);
  REQUIRE(target_function(js).function_abi.invocation.count_parameter != name);
  REQUIRE(target_function(cpp).function_abi.invocation.count_parameter != name);
}

TEST_CASE("invocation queries and bound bare calls retain deterministic target source maps") {
  const std::string text =
      "checked;\nfunction [first,second] = checked(input)\narguments\n"
      "input (1,1) double = 7\nend\nfirst = input + nargout;\n"
      "second = nargout();\nend\n";
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::matlab;
    options.target = target;
    options.filename = "invocation-map.m";
    options.emit_source_banner = false;
    const auto result = mpf::Transpiler{}.transpile(text, options);
    REQUIRE(result.success());
    for (const auto line : {1U, 4U, 6U, 7U})
      REQUIRE(std::any_of(result.source_map.segments.begin(), result.source_map.segments.end(),
                          [&](const auto& segment) { return segment.original_line == line; }));
    const auto repeated = mpf::Transpiler{}.transpile(text, options);
    REQUIRE(repeated.success());
    REQUIRE(result.code == repeated.code);
    REQUIRE(result.source_map.to_json() == repeated.source_map.to_json());
  }
}
