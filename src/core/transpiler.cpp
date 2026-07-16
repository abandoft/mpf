#include "mpf/transpiler.hpp"

#include <algorithm>
#include <cctype>
#include <new>
#include <string>
#include <vector>

#include "../compiler/code_binding.hpp"
#include "../compiler/frontend.hpp"
#include "../frontends/frontend_registry.hpp"
#include "../ir/hir.hpp"
#include "../ir/mir.hpp"
#include "../ir/pass_manager.hpp"
#include "../semantic/analyzer.hpp"
#include "backend_registry.hpp"
#include "compilation_session.hpp"
#include "source_map_builder.hpp"

namespace mpf {
namespace {

bool equals_ci(const std::string_view left, const std::string_view right) noexcept {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(left[index])) !=
        std::tolower(static_cast<unsigned char>(right[index]))) {
      return false;
    }
  }
  return true;
}

struct SourceComplexity {
  std::size_t tokens{0};
  std::size_t depth{0};
};

SourceComplexity inspect_source(const std::string_view source, const SourceLanguage language) {
  SourceComplexity result;
  std::size_t bracket_depth = 0;
  bool in_word = false;
  for (const char raw_character : source) {
    const auto character = static_cast<unsigned char>(raw_character);
    const bool word = std::isalnum(character) != 0 || character == '_';
    if (word && !in_word) ++result.tokens;
    if (!word && std::isspace(character) == 0) ++result.tokens;
    in_word = word;
    if (character == '(' || character == '[' || character == '{') {
      ++bracket_depth;
      result.depth = std::max(result.depth, bracket_depth);
    } else if ((character == ')' || character == ']' || character == '}') && bracket_depth != 0) {
      --bracket_depth;
    }
  }

  std::vector<std::size_t> indentation{0};
  std::size_t keyword_depth = 0;
  std::size_t cursor = 0;
  while (cursor < source.size()) {
    const auto end = source.find('\n', cursor);
    const auto line = source.substr(
        cursor, end == std::string_view::npos ? source.size() - cursor : end - cursor);
    std::size_t leading_bytes = 0;
    std::size_t indentation_width = 0;
    while (leading_bytes < line.size() &&
           (line[leading_bytes] == ' ' || line[leading_bytes] == '\t')) {
      indentation_width += line[leading_bytes] == '\t' ? 8U : 1U;
      ++leading_bytes;
    }
    const auto content = line.substr(leading_bytes);
    if (!content.empty() && content.front() != '#' && content.front() != '%' &&
        content.front() != '!') {
      if (language == SourceLanguage::python) {
        while (indentation.size() > 1 && indentation_width < indentation.back()) {
          indentation.pop_back();
        }
        if (indentation_width > indentation.back()) indentation.push_back(indentation_width);
        result.depth = std::max(result.depth, indentation.size() - 1U);
      } else {
        std::string first;
        for (const char raw_character : content) {
          const auto character = static_cast<unsigned char>(raw_character);
          if (std::isalpha(character) == 0) break;
          first.push_back(static_cast<char>(std::tolower(character)));
        }
        if (first == "end" || first == "endif" || first == "enddo" || first == "endselect") {
          if (keyword_depth != 0) --keyword_depth;
        } else if (first == "if" || first == "for" || first == "while" || first == "do" ||
                   first == "select" || first == "function" || first == "program" ||
                   first == "subroutine") {
          ++keyword_depth;
          result.depth = std::max(result.depth, keyword_depth);
        }
      }
    }
    if (end == std::string_view::npos) break;
    cursor = end + 1U;
  }
  return result;
}

}  // namespace

bool TranspileResult::success() const noexcept {
  return std::none_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
    return diagnostic.severity == DiagnosticSeverity::error;
  });
}

TranspileResult Transpiler::transpile(const std::string_view source,
                                      const TranspileOptions& options) const {
  TranspileResult result;
  detail::CompilationSession session(options.resource_limits);
  const auto finalize_report = [&] {
    result.report.source_bytes = source.size();
    result.report.total_nanoseconds = session.elapsed_nanoseconds();
    result.report.peak_arena_bytes = session.peak_arena_bytes();
    result.report.stages.clear();
    result.report.stages.reserve(session.metrics().size());
    for (const auto& metric : session.metrics()) {
      result.report.stages.push_back(
          {metric.stage, metric.nodes, metric.duration_nanoseconds, metric.arena_bytes});
    }
  };
  const auto resource_error = [&](const std::string_view stage, const std::size_t actual,
                                  const std::size_t limit) {
    result.diagnostics.push_back({DiagnosticSeverity::error,
                                  "MPF0010",
                                  "resource limit exceeded at '" + std::string(stage) + "': " +
                                      std::to_string(actual) + " exceeds " + std::to_string(limit),
                                  {1, 1}});
  };
  if (source.size() > session.limits().max_source_bytes) {
    resource_error("source-bytes", source.size(), session.limits().max_source_bytes);
    result.diagnostics.back().source_name =
        options.filename.empty() ? "<memory>" : options.filename;
    finalize_report();
    return result;
  }
  auto language = options.language;
  if (language == SourceLanguage::automatic) {
    if (options.fortran_source_form != FortranSourceForm::automatic) {
      language = SourceLanguage::fortran;
    } else if (const auto* detected = detail::detect_frontend(source, options.filename)) {
      language = detected->language;
    }
  }
  const auto source_id = session.sources().add(source, options.filename);
  const auto& source_text = session.sources().source(source_id);
  const auto attach_source = [&] {
    for (auto& diagnostic : result.diagnostics) {
      diagnostic.source_id = source_id;
      if (diagnostic.source_name.empty()) {
        diagnostic.source_name = options.filename.empty() ? "<memory>" : options.filename;
      }
    }
  };
  const auto complexity_started = session.begin_stage();
  const auto complexity = inspect_source(source, language);
  session.record("source-complexity", complexity.tokens, complexity_started);
  if (complexity.tokens > session.limits().max_tokens) {
    resource_error("tokens", complexity.tokens, session.limits().max_tokens);
  }
  if (complexity.depth > session.limits().max_parser_depth) {
    resource_error("parser-depth", complexity.depth, session.limits().max_parser_depth);
  }
  if (!result.success()) {
    attach_source();
    finalize_report();
    return result;
  }
  const auto* backend = detail::find_backend(options.target);
  if (backend == nullptr) {
    result.diagnostics.push_back({DiagnosticSeverity::error,
                                  "MPF0003",
                                  "requested backend '" + std::string(to_string(options.target)) +
                                      "' is not available in this MPF build",
                                  {1, 1}});
    attach_source();
    finalize_report();
    return result;
  }
  const auto* frontend = detail::find_frontend(language);
  detail::FrontendParseResult parsed;
  const auto ast_started = session.begin_stage();
  if (frontend != nullptr && frontend->parse != nullptr) {
    try {
      parsed =
          frontend->parse(source_text, {options.fortran_source_form, session.memory_resource()});
    } catch (const std::bad_alloc&) {
      parsed.diagnostics.push_back({DiagnosticSeverity::error,
                                    "MPF0010",
                                    "resource limit exceeded at 'arena-bytes'",
                                    {1, 1}});
    }
  } else {
    parsed.diagnostics.push_back(
        {DiagnosticSeverity::error, "MPF0002", "source language could not be determined", {1, 1}});
  }
  result.diagnostics = std::move(parsed.diagnostics);
  if (result.success()) {
    const auto nodes = detail::frontend_ast_node_count(parsed.ast);
    session.record("ast", nodes, ast_started);
    if (nodes > session.limits().max_ast_nodes) {
      resource_error("ast-nodes", nodes, session.limits().max_ast_nodes);
    }
    const auto arena_bytes = detail::frontend_ast_arena_bytes(parsed.ast);
    if (arena_bytes > session.limits().max_arena_bytes) {
      resource_error("arena-bytes", arena_bytes, session.limits().max_arena_bytes);
    }
  }
  detail::hir::LoweringResult hir_result;
  if (result.success()) {
    if (frontend == nullptr || frontend->verify == nullptr || frontend->lower == nullptr) {
      result.diagnostics.push_back({DiagnosticSeverity::error,
                                    "MPF0002",
                                    "source frontend has an incomplete AST contract",
                                    {1, 1}});
    } else {
      auto ast_diagnostics = frontend->verify(parsed.ast);
      result.diagnostics.insert(result.diagnostics.end(),
                                std::make_move_iterator(ast_diagnostics.begin()),
                                std::make_move_iterator(ast_diagnostics.end()));
      if (result.success()) {
        const auto hir_started = session.begin_stage();
        hir_result = frontend->lower(std::move(parsed.ast));
        session.record("hir", hir_result.program.node_count, hir_started);
        if (hir_result.program.node_count > session.limits().max_hir_nodes) {
          resource_error("hir-nodes", hir_result.program.node_count,
                         session.limits().max_hir_nodes);
        }
      }
    }
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(hir_result.diagnostics.begin()),
                              std::make_move_iterator(hir_result.diagnostics.end()));
  }
  if (result.success()) {
    const auto analysis_started = session.begin_stage();
    detail::PassManager<detail::hir::Program> passes(&detail::hir::verify);
    passes.add({"semantic-analysis", &detail::analyze_program, true});
    auto semantic_diagnostics = passes.run(hir_result.program);
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(semantic_diagnostics.begin()),
                              std::make_move_iterator(semantic_diagnostics.end()));
    session.record("hir-passes", hir_result.program.node_count, analysis_started);
  }
  detail::mir::LoweringResult mir_result;
  if (result.success()) {
    const auto mir_started = session.begin_stage();
    mir_result = detail::mir::lower_from_hir(std::move(hir_result.program));
    session.record("mir", mir_result.program.instructions.size() - 1U, mir_started);
    const auto instructions = mir_result.program.instructions.size() - 1U;
    if (instructions > session.limits().max_mir_instructions) {
      resource_error("mir-instructions", instructions, session.limits().max_mir_instructions);
    }
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(mir_result.diagnostics.begin()),
                              std::make_move_iterator(mir_result.diagnostics.end()));
  }
  if (result.success()) {
    const auto pass_started = session.begin_stage();
    detail::PassManager<detail::mir::Program> passes(&detail::mir::verify);
    passes.add({"effect-validation", &detail::mir::validate_effects, false});
    auto mir_diagnostics = passes.run(mir_result.program);
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(mir_diagnostics.begin()),
                              std::make_move_iterator(mir_diagnostics.end()));
    session.record("mir-passes", mir_result.program.instructions.size() - 1U, pass_started);
  }
  if (result.success()) {
    auto binding_diagnostics =
        detail::validate_code_bindings(mir_result.program, backend->binding, backend->name);
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(binding_diagnostics.begin()),
                              std::make_move_iterator(binding_diagnostics.end()));
  }
  if (result.success()) {
    auto capability_diagnostics = backend->validate(mir_result.program);
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(capability_diagnostics.begin()),
                              std::make_move_iterator(capability_diagnostics.end()));
  }
  detail::BackendLoweringResult target_result;
  if (result.success()) {
    const auto target_started = session.begin_stage();
    target_result = backend->lower(mir_result.program, options);
    if (target_result.artifact != nullptr) {
      const auto nodes = target_result.artifact->node_count_hint();
      session.record("target-lir", nodes, target_started);
      if (nodes > session.limits().max_lir_nodes) {
        resource_error("target-lir-nodes", nodes, session.limits().max_lir_nodes);
      }
    }
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(target_result.diagnostics.begin()),
                              std::make_move_iterator(target_result.diagnostics.end()));
  }
  if (result.success() && target_result.artifact != nullptr) {
    auto target_diagnostics = backend->verify(*target_result.artifact);
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(target_diagnostics.begin()),
                              std::make_move_iterator(target_diagnostics.end()));
  }
  if (result.success() && target_result.artifact != nullptr) {
    const auto emission_started = session.begin_stage();
    result.dependencies.reserve(target_result.artifact->dependencies().size());
    for (const auto dependency : target_result.artifact->dependencies()) {
      result.dependencies.emplace_back(dependency);
    }
    result.code = backend->emit(*target_result.artifact, options);
    if (result.code.size() > session.limits().max_generated_bytes) {
      resource_error("generated-bytes", result.code.size(), session.limits().max_generated_bytes);
      result.code.clear();
      result.dependencies.clear();
    }
    if (result.success() && options.emit_source_map) {
      const auto source_name = options.filename.empty() ? std::string_view{"<memory>"}
                                                        : std::string_view{options.filename};
      const auto generated_name = options.generated_filename.empty()
                                      ? std::string(to_string(options.target)) +
                                            (options.target == TargetLanguage::cpp ? ".cpp" : ".js")
                                      : options.generated_filename;
      result.source_map = detail::build_source_map(target_result.artifact->serialized_chunks(),
                                                   source_name, generated_name);
      const auto source_map_size = result.source_map.to_json().size();
      if (source_map_size > session.limits().max_source_map_bytes) {
        resource_error("source-map-bytes", source_map_size, session.limits().max_source_map_bytes);
        result.code.clear();
        result.source_map = {};
        result.dependencies.clear();
      }
    }
    session.record("emission", result.code.size(), emission_started);
  }
  attach_source();
  finalize_report();
  return result;
}

const char* to_string(const SourceLanguage language) noexcept {
  if (language == SourceLanguage::automatic) return "auto";
  const auto* frontend = detail::find_frontend(language);
  return frontend == nullptr ? "auto" : frontend->name;
}

SourceLanguage language_from_name(const std::string_view name) noexcept {
  if (equals_ci(name, "auto")) return SourceLanguage::automatic;
  const auto* frontend = detail::find_frontend(name);
  return frontend == nullptr ? SourceLanguage::automatic : frontend->language;
}

bool frontend_available(const SourceLanguage language) noexcept {
  return detail::find_frontend(language) != nullptr;
}

bool source_language_name_known(const std::string_view name) noexcept {
  return equals_ci(name, "auto") || detail::find_frontend(name) != nullptr;
}

std::vector<SourceLanguage> registered_source_languages() {
  std::vector<SourceLanguage> languages;
  languages.reserve(detail::frontend_count());
  for (std::size_t index = 0; index < detail::frontend_count(); ++index) {
    languages.push_back(detail::frontend_at(index)->language);
  }
  return languages;
}

const char* to_string(const TargetLanguage language) noexcept {
  const auto* backend = detail::find_backend_descriptor(language);
  return backend == nullptr ? "javascript" : backend->name;
}

TargetLanguage target_from_name(const std::string_view name) noexcept {
  const auto* backend = detail::find_backend_descriptor(name);
  return backend == nullptr ? TargetLanguage::javascript : backend->target;
}

bool target_language_name_known(const std::string_view name) noexcept {
  return detail::find_backend_descriptor(name) != nullptr;
}

std::vector<TargetLanguage> registered_target_languages() {
  std::vector<TargetLanguage> languages;
  languages.reserve(detail::backend_descriptor_count());
  for (std::size_t index = 0; index < detail::backend_descriptor_count(); ++index) {
    languages.push_back(detail::backend_descriptor_at(index)->target);
  }
  return languages;
}

const char* to_string(const FortranSourceForm form) noexcept {
  switch (form) {
    case FortranSourceForm::automatic: return "auto";
    case FortranSourceForm::free: return "free";
    case FortranSourceForm::fixed: return "fixed";
  }
  return "auto";
}

FortranSourceForm fortran_source_form_from_name(const std::string_view name) noexcept {
  if (equals_ci(name, "free")) return FortranSourceForm::free;
  if (equals_ci(name, "fixed")) return FortranSourceForm::fixed;
  return FortranSourceForm::automatic;
}

bool backend_available(const TargetLanguage language) noexcept {
  return detail::find_backend(language) != nullptr;
}

}  // namespace mpf
