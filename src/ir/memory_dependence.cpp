#include "memory_dependence.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mpf::detail::mir {
namespace {

template <typename Id, typename Item>
bool valid_index(const Id id, const std::vector<Item>& items) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < items.size();
}

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               const std::string_view stage, std::string message) {
  diagnostics.push_back(
      {DiagnosticSeverity::error, "MPF0006",
       "invalid MIR memory-dependence table at '" + std::string(stage) + "': " + std::move(message),
       location});
}

MemoryAccessMode unknown_mode(const InstructionEffectFacts& facts) noexcept {
  if (facts.reads_unknown && facts.writes_unknown) return MemoryAccessMode::read_write;
  if (facts.reads_unknown) return MemoryAccessMode::read;
  if (facts.writes_unknown) return MemoryAccessMode::write;
  return MemoryAccessMode::none;
}

std::vector<MemoryAccessSite> access_sites(const InstructionEffectFacts& facts) {
  std::vector<MemoryAccessSite> result;
  result.reserve(facts.memory_accesses.size() + 1U);
  for (std::size_t index = 0; index < facts.memory_accesses.size(); ++index) {
    result.push_back({facts.origin, static_cast<std::uint32_t>(index),
                      facts.memory_accesses[index].mode, false});
  }
  const auto mode = unknown_mode(facts);
  if (mode != MemoryAccessMode::none) result.push_back({facts.origin, 0, mode, true});
  return result;
}

const MemoryAccess* access_for_site(const AliasEffectTable& alias_effects,
                                    const MemoryAccessSite& site) noexcept {
  if (site.unknown) return nullptr;
  const auto* facts = alias_effects.instruction(site.instruction);
  return facts != nullptr && site.ordinal < facts->memory_accesses.size()
             ? &facts->memory_accesses[site.ordinal]
             : nullptr;
}

AliasClass site_relation(const AliasEffectTable& alias_effects, const MemoryAccessSite& left,
                         const MemoryAccessSite& right) noexcept {
  if (left.unknown || right.unknown) return AliasClass::may_alias;
  const auto* left_access = access_for_site(alias_effects, left);
  const auto* right_access = access_for_site(alias_effects, right);
  return left_access == nullptr || right_access == nullptr
             ? AliasClass::may_alias
             : alias_between(alias_effects, *left_access, *right_access);
}

struct FrontierAccess {
  MemoryAccessSite site;
  bool loop_carried{false};
};

auto frontier_key(const FrontierAccess& access) noexcept {
  return std::tuple{access.site.instruction.value(), access.site.unknown, access.site.ordinal,
                    static_cast<std::uint8_t>(access.site.mode), access.loop_carried};
}

bool operator==(const FrontierAccess& left, const FrontierAccess& right) noexcept {
  return left.site == right.site && left.loop_carried == right.loop_carried;
}

bool frontier_less(const FrontierAccess& left, const FrontierAccess& right) noexcept {
  return frontier_key(left) < frontier_key(right);
}

void normalize_frontier(std::vector<FrontierAccess>& frontier) {
  std::sort(frontier.begin(), frontier.end(), &frontier_less);
  frontier.erase(std::unique(frontier.begin(), frontier.end()), frontier.end());
}

bool merge_frontier(std::vector<FrontierAccess>& target, const std::vector<FrontierAccess>& source,
                    const bool loop_edge) {
  std::vector<FrontierAccess> incoming;
  incoming.reserve(source.size());
  for (auto access : source) {
    access.loop_carried = access.loop_carried || loop_edge;
    incoming.push_back(access);
  }
  normalize_frontier(incoming);
  std::vector<FrontierAccess> merged;
  merged.reserve(target.size() + incoming.size());
  std::set_union(target.begin(), target.end(), incoming.begin(), incoming.end(),
                 std::back_inserter(merged), &frontier_less);
  if (merged.size() == target.size()) return false;
  target = std::move(merged);
  return true;
}

void update_frontier(const AliasEffectTable& alias_effects, std::vector<FrontierAccess>& frontier,
                     const std::vector<MemoryAccessSite>& current) {
  for (const auto& site : current) {
    if (!memory_access_writes(site.mode) || site.unknown) continue;
    frontier.erase(std::remove_if(frontier.begin(), frontier.end(),
                                  [&](const FrontierAccess& previous) {
                                    return !previous.site.unknown &&
                                           site_relation(alias_effects, previous.site, site) ==
                                               AliasClass::must_alias;
                                  }),
                   frontier.end());
  }
  for (const auto& site : current) frontier.push_back({site, false});
  normalize_frontier(frontier);
}

std::vector<FrontierAccess> transfer_block(const AliasEffectTable& alias_effects,
                                           const BasicBlock& block,
                                           std::vector<FrontierAccess> frontier) {
  for (const auto instruction_id : block.instructions) {
    const auto* facts = alias_effects.instruction(instruction_id);
    if (facts == nullptr) continue;
    update_frontier(alias_effects, frontier, access_sites(*facts));
  }
  return frontier;
}

struct FunctionGraph {
  std::vector<BlockId> blocks;
  std::vector<std::size_t> local_index;
  std::vector<std::vector<std::size_t>> successors;
  std::vector<std::vector<bool>> loop_edges;
};

FunctionGraph build_function_graph(const Program& program, const Function& function) {
  const auto absent = std::numeric_limits<std::size_t>::max();
  FunctionGraph graph;
  graph.blocks = function.blocks;
  graph.local_index.assign(program.blocks.size(), absent);
  for (std::size_t index = 0; index < graph.blocks.size(); ++index) {
    if (valid_index(graph.blocks[index], program.blocks)) {
      graph.local_index[graph.blocks[index].value()] = index;
    }
  }
  graph.successors.resize(graph.blocks.size());
  for (std::size_t index = 0; index < graph.blocks.size(); ++index) {
    if (!valid_index(graph.blocks[index], program.blocks)) continue;
    for (const auto successor : program.blocks[graph.blocks[index].value()].terminator.successors) {
      if (!valid_index(successor, program.blocks)) continue;
      const auto target = graph.local_index[successor.value()];
      if (target == absent) continue;
      graph.successors[index].push_back(target);
    }
    std::sort(graph.successors[index].begin(), graph.successors[index].end());
    graph.successors[index].erase(
        std::unique(graph.successors[index].begin(), graph.successors[index].end()),
        graph.successors[index].end());
  }
  const auto count = graph.blocks.size();
  graph.loop_edges.resize(count);
  for (std::size_t block = 0; block < count; ++block) {
    graph.loop_edges[block].resize(graph.successors[block].size(), false);
  }
  enum class Visit : std::uint8_t { white, gray, black };
  struct DfsFrame {
    std::size_t block{0};
    std::size_t next_successor{0};
  };
  std::vector<Visit> visits(count, Visit::white);
  std::vector<DfsFrame> stack;
  stack.reserve(count);
  for (std::size_t start = 0; start < count; ++start) {
    if (visits[start] != Visit::white) continue;
    visits[start] = Visit::gray;
    stack.push_back({start, 0});
    while (!stack.empty()) {
      auto& frame = stack.back();
      if (frame.next_successor == graph.successors[frame.block].size()) {
        visits[frame.block] = Visit::black;
        stack.pop_back();
        continue;
      }
      const auto edge = frame.next_successor++;
      const auto successor = graph.successors[frame.block][edge];
      if (visits[successor] == Visit::gray) {
        graph.loop_edges[frame.block][edge] = true;
      } else if (visits[successor] == Visit::white) {
        visits[successor] = Visit::gray;
        stack.push_back({successor, 0});
      }
    }
  }
  return graph;
}

struct DependenceDraft {
  MemoryAccessSite source;
  MemoryAccessSite target;
  MemoryDependenceKind kind{MemoryDependenceKind::flow};
  AliasClass relation{AliasClass::may_alias};
  bool barrier{false};
  bool loop_carried{false};
};

auto draft_key(const DependenceDraft& dependence) noexcept {
  return std::tuple{dependence.target.instruction.value(),
                    dependence.target.unknown,
                    dependence.target.ordinal,
                    dependence.source.instruction.value(),
                    dependence.source.unknown,
                    dependence.source.ordinal,
                    static_cast<std::uint8_t>(dependence.kind)};
}

void add_hazard(std::vector<DependenceDraft>& dependences, const FrontierAccess& source,
                const MemoryAccessSite& target, const MemoryDependenceKind kind,
                const AliasClass relation) {
  dependences.push_back({source.site, target, kind, relation, source.site.unknown || target.unknown,
                         source.loop_carried});
}

void collect_instruction_dependences(const AliasEffectTable& alias_effects,
                                     const InstructionEffectFacts& current_facts,
                                     const std::vector<FrontierAccess>& frontier,
                                     std::vector<DependenceDraft>& dependences) {
  const auto current = access_sites(current_facts);
  for (const auto& target : current) {
    for (const auto& source : frontier) {
      if (source.site.instruction == target.instruction && !source.loop_carried) continue;
      const auto relation = site_relation(alias_effects, source.site, target);
      if (relation == AliasClass::no_alias) continue;
      if (memory_access_writes(source.site.mode) && memory_access_reads(target.mode)) {
        add_hazard(dependences, source, target, MemoryDependenceKind::flow, relation);
      }
      if (memory_access_reads(source.site.mode) && memory_access_writes(target.mode)) {
        add_hazard(dependences, source, target, MemoryDependenceKind::anti, relation);
      }
      if (memory_access_writes(source.site.mode) && memory_access_writes(target.mode)) {
        add_hazard(dependences, source, target, MemoryDependenceKind::output, relation);
      }
    }
  }
}

void analyze_function(const Program& program, const AliasEffectTable& alias_effects,
                      const Function& function, std::vector<DependenceDraft>& dependences) {
  const auto graph = build_function_graph(program, function);
  const auto count = graph.blocks.size();
  std::vector<std::vector<FrontierAccess>> inputs(count);
  std::vector<std::vector<FrontierAccess>> outputs(count);
  std::deque<std::size_t> worklist;
  std::vector<bool> queued(count, true);
  for (std::size_t index = 0; index < count; ++index) worklist.push_back(index);
  while (!worklist.empty()) {
    const auto block = worklist.front();
    worklist.pop_front();
    queued[block] = false;
    if (!valid_index(graph.blocks[block], program.blocks)) continue;
    auto output =
        transfer_block(alias_effects, program.blocks[graph.blocks[block].value()], inputs[block]);
    if (output == outputs[block]) continue;
    outputs[block] = std::move(output);
    for (std::size_t edge = 0; edge < graph.successors[block].size(); ++edge) {
      const auto successor = graph.successors[block][edge];
      if (!merge_frontier(inputs[successor], outputs[block], graph.loop_edges[block][edge]) ||
          queued[successor]) {
        continue;
      }
      worklist.push_back(successor);
      queued[successor] = true;
    }
  }

  for (std::size_t block = 0; block < count; ++block) {
    if (!valid_index(graph.blocks[block], program.blocks)) continue;
    auto frontier = inputs[block];
    for (const auto instruction_id : program.blocks[graph.blocks[block].value()].instructions) {
      const auto* facts = alias_effects.instruction(instruction_id);
      if (facts == nullptr) continue;
      collect_instruction_dependences(alias_effects, *facts, frontier, dependences);
      update_frontier(alias_effects, frontier, access_sites(*facts));
    }
  }
}

bool same_dependence(const MemoryDependence& left, const MemoryDependence& right) noexcept {
  return left.id == right.id && left.source == right.source && left.target == right.target &&
         left.kind == right.kind && left.relation == right.relation &&
         left.barrier == right.barrier && left.loop_carried == right.loop_carried;
}

bool valid_site(const AliasEffectTable& alias_effects, const MemoryAccessSite& site) noexcept {
  const auto* facts = alias_effects.instruction(site.instruction);
  if (facts == nullptr || site.mode == MemoryAccessMode::none) return false;
  if (site.unknown) return site.ordinal == 0U && site.mode == unknown_mode(*facts);
  return site.ordinal < facts->memory_accesses.size() &&
         site.mode == facts->memory_accesses[site.ordinal].mode;
}

}  // namespace

const InstructionMemoryDependenceFacts* MemoryDependenceTable::instruction(
    const InstructionId id) const noexcept {
  return id.valid() && id.value() < instructions.size() ? &instructions[id.value()] : nullptr;
}

const MemoryDependence* MemoryDependenceTable::dependence(
    const MemoryDependenceId id) const noexcept {
  return id.valid() && id.value() < dependences.size() ? &dependences[id.value()] : nullptr;
}

MemoryDependenceTable analyze_memory_dependences(const Program& program,
                                                 const AliasEffectTable& alias_effects) {
  MemoryDependenceTable result;
  result.mir_revision = program.revision;
  result.instruction_count = program.instructions.size() - (program.instructions.empty() ? 0U : 1U);
  result.instructions.resize(program.instructions.size());
  result.dependences.resize(1U);
  for (std::size_t index = 1; index < result.instructions.size(); ++index) {
    result.instructions[index].origin =
        InstructionId{static_cast<InstructionId::value_type>(index)};
  }
  if (!alias_effects_current(program, alias_effects)) return result;

  std::vector<DependenceDraft> drafts;
  for (std::size_t index = 1; index < program.functions.size(); ++index) {
    analyze_function(program, alias_effects, program.functions[index], drafts);
  }
  std::sort(drafts.begin(), drafts.end(),
            [](const auto& left, const auto& right) { return draft_key(left) < draft_key(right); });
  std::vector<DependenceDraft> canonical;
  canonical.reserve(drafts.size());
  for (const auto& draft : drafts) {
    if (canonical.empty() || draft_key(canonical.back()) != draft_key(draft)) {
      canonical.push_back(draft);
      continue;
    }
    auto& existing = canonical.back();
    existing.loop_carried = existing.loop_carried || draft.loop_carried;
    existing.barrier = existing.barrier || draft.barrier;
    if (existing.relation != draft.relation) existing.relation = AliasClass::may_alias;
  }
  for (const auto& draft : canonical) {
    const auto id =
        MemoryDependenceId{static_cast<MemoryDependenceId::value_type>(result.dependences.size())};
    result.dependences.push_back({id, draft.source, draft.target, draft.kind, draft.relation,
                                  draft.barrier, draft.loop_carried});
    result.instructions[draft.target.instruction.value()].incoming.push_back(id);
    result.instructions[draft.source.instruction.value()].outgoing.push_back(id);
  }
  for (auto& facts : result.instructions) {
    std::sort(facts.incoming.begin(), facts.incoming.end());
    std::sort(facts.outgoing.begin(), facts.outgoing.end());
  }
  result.dependence_count = result.dependences.size() - 1U;
  result.complete = true;
  return result;
}

bool memory_dependences_current(const Program& program, const AliasEffectTable& alias_effects,
                                const MemoryDependenceTable& analysis) noexcept {
  return analysis.complete && alias_effects_current(program, alias_effects) &&
         analysis.mir_revision == program.revision &&
         analysis.instruction_count + 1U == program.instructions.size() &&
         analysis.dependence_count + 1U == analysis.dependences.size() &&
         analysis.instructions.size() == program.instructions.size();
}

std::vector<Diagnostic> verify_memory_dependences(const Program& program,
                                                  const AliasEffectTable& alias_effects,
                                                  const MemoryDependenceTable& analysis,
                                                  const std::string_view stage) {
  std::vector<Diagnostic> diagnostics;
  if (!verify_alias_effects(program, alias_effects, stage).empty()) {
    add_error(diagnostics, {1, 1}, stage,
              "analysis requires a current verified alias/effect table");
    return diagnostics;
  }
  if (!memory_dependences_current(program, alias_effects, analysis)) {
    add_error(diagnostics, {1, 1}, stage, "revision or dense analysis inventory is stale");
    return diagnostics;
  }
  if (analysis.instructions.front().origin.valid() ||
      !analysis.instructions.front().incoming.empty() ||
      !analysis.instructions.front().outgoing.empty() || analysis.dependences.front().id.valid() ||
      analysis.dependences.front().source.instruction.valid() ||
      analysis.dependences.front().target.instruction.valid()) {
    add_error(diagnostics, {1, 1}, stage, "dense analysis sentinels contain resident facts");
  }
  const auto expected = analyze_memory_dependences(program, alias_effects);
  for (std::size_t index = 1; index < analysis.instructions.size(); ++index) {
    const auto& actual = analysis.instructions[index];
    const auto& wanted = expected.instructions[index];
    if (actual.origin.value() != index || actual.origin != wanted.origin ||
        actual.incoming != wanted.incoming || actual.outgoing != wanted.outgoing) {
      add_error(diagnostics, program.instructions[index].location, stage,
                "instruction dependence adjacency is invalid, unsorted, or stale");
    }
  }
  if (analysis.dependences.size() != expected.dependences.size()) {
    add_error(diagnostics, {1, 1}, stage, "dependence count disagrees with the CFG fixed point");
    return diagnostics;
  }
  for (std::size_t index = 1; index < analysis.dependences.size(); ++index) {
    const auto& actual = analysis.dependences[index];
    const auto location = valid_index(actual.target.instruction, program.instructions)
                              ? program.instructions[actual.target.instruction.value()].location
                              : SourceLocation{1, 1};
    if (actual.id.value() != index || !valid_site(alias_effects, actual.source) ||
        !valid_site(alias_effects, actual.target) || actual.relation == AliasClass::no_alias ||
        actual.barrier != (actual.source.unknown || actual.target.unknown) ||
        !same_dependence(actual, expected.dependences[index])) {
      add_error(diagnostics, location, stage,
                "dependence edge is invalid, non-dense, or disagrees with regional aliasing");
    }
  }
  return diagnostics;
}

}  // namespace mpf::detail::mir
