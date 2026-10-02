#include "mir_invocation_context.hpp"

#include <string>
#include <unordered_set>

namespace mpf::detail::mir {

bool is_output_count_query(const Program& program, const Expression& expression) {
  const auto* facts = attributes(program, expression.id);
  if (facts == nullptr) return false;
  if (expression.kind == ExpressionKind::identifier)
    return facts->binding == BindingKind::builtin &&
           facts->intrinsic == IntrinsicId::matlab_nargout &&
           value_type(program, expression.type_id) == ValueType::real;
  if (expression.kind != ExpressionKind::call || expression.children.empty()) return false;
  const auto* callee = attributes(program, expression.children.front());
  return callee != nullptr && callee->binding == BindingKind::builtin &&
         callee->intrinsic == IntrinsicId::matlab_nargout;
}

void verify_invocation_contexts(const Program& program, std::vector<Diagnostic>& diagnostics,
                                const std::string_view stage) {
  const auto fail = [&](const SourceLocation location, const char* message) {
    diagnostics.push_back({DiagnosticSeverity::error, "MPF0006",
                           "invalid MIR at '" + std::string(stage) + "': " + message, location});
  };
  std::vector<const Function*> owners(program.instructions.size(), nullptr);
  std::unordered_set<ValueId> frame_values;
  const auto valid_count_type = [&](const TypeId id, const ShapeId shape_id) {
    const auto* count_type = type(program, id);
    const auto* shape_data = shape(program, shape_id);
    return count_type != nullptr && count_type->kind == TypeKind::scalar &&
           value_type(program, id) == ValueType::real &&
           numeric_type(program, id) == real_numeric_type && shape_data != nullptr &&
           !shape_data->dynamic_rank && shape_data->extents.empty();
  };
  for (const auto& function : program.functions) {
    if (!function.id.valid()) continue;
    const auto& frame = function.invocation_frame;
    const bool expected =
        program.source_language == SourceLanguage::matlab && function.origin.valid();
    if (!expected) {
      if (frame != InvocationFrame{})
        fail({1U, 1U}, "non-Matlab function owns an invocation frame");
    } else if (!frame.active() || !frame_values.insert(frame.output_count).second ||
               !valid_count_type(frame.type, frame.shape) || !function.entry.valid() ||
               function.entry.value() >= program.blocks.size()) {
      fail({1U, 1U}, "Matlab function has no typed invocation count formal");
    } else {
      const auto& arguments = program.blocks[function.entry.value()].arguments;
      if (arguments.size() != function.parameter_types.size() + 1U ||
          arguments.back().value != frame.output_count || arguments.back().type != frame.type ||
          arguments.back().shape != frame.shape || arguments.back().storage.valid())
        fail({1U, 1U}, "invocation count is not a separate trailing entry formal");
    }
    for (const auto block_id : function.blocks) {
      if (!block_id.valid() || block_id.value() >= program.blocks.size()) continue;
      for (const auto instruction_id : program.blocks[block_id.value()].instructions)
        if (instruction_id.valid() && instruction_id.value() < owners.size())
          owners[instruction_id.value()] = &function;
    }
  }
  std::vector<const CallSite*> calls(program.instructions.size(), nullptr);
  for (const auto& call : program.calls) {
    if (!call.instruction.valid() || call.instruction.value() >= calls.size()) continue;
    calls[call.instruction.value()] = &call;
    InvocationDemand expected;
    if (call.callee.valid() && call.callee.value() < program.functions.size()) {
      const auto& frame = program.functions[call.callee.value()].invocation_frame;
      if (frame.active()) expected = {frame.type, frame.shape, call.output_demand.count};
    }
    if (call.invocation_demand != expected ||
        program.instructions[call.instruction.value()].invocation_demand != expected ||
        (expected.active() && (!call.output_demand.active() || !call.output_demand.valid())))
      fail({1U, 1U}, "call invocation count disagrees with its callee and source context");
  }
  std::vector<bool> queries(program.instructions.size(), false);
  for (const auto& expression : program.expressions) {
    if (!expression.instruction.valid() || expression.instruction.value() >= owners.size())
      continue;
    const auto& instruction = program.instructions[expression.instruction.value()];
    const bool query = is_output_count_query(program, expression);
    if (!query) continue;
    queries[expression.instruction.value()] = true;
    const auto* owner = owners[expression.instruction.value()];
    if (program.source_language != SourceLanguage::matlab || owner == nullptr ||
        !owner->invocation_frame.active() ||
        instruction.opcode != Opcode::invocation_output_count ||
        instruction.intrinsic != IntrinsicId::matlab_nargout || instruction.callee.valid() ||
        instruction.storage.valid() || !valid_count_type(expression.type_id, expression.shape_id) ||
        instruction.operands != std::vector<ValueId>{owner->invocation_frame.output_count} ||
        (expression.kind == ExpressionKind::call && expression.children.size() != 1U))
      fail(expression.location, "nargout query does not read its own typed invocation formal");
  }
  for (const auto& instruction : program.instructions) {
    if (!instruction.id.valid() || instruction.id.value() >= queries.size()) continue;
    if ((instruction.opcode == Opcode::invocation_output_count) != queries[instruction.id.value()])
      fail(instruction.location, "invocation count instruction has no matching source query");
    if (calls[instruction.id.value()] == nullptr &&
        instruction.invocation_demand != InvocationDemand{})
      fail(instruction.location, "non-user-call instruction retains invocation metadata");
  }
}

}  // namespace mpf::detail::mir
