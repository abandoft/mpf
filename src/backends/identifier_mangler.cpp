#include "identifier_mangler.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <unordered_set>

namespace mpf::detail {
namespace {

bool reserved(const std::string& name, const TargetLanguage target) {
  static const std::unordered_set<std::string> javascript{
      "await",    "break",   "case",       "catch",     "class",    "const",  "continue",
      "debugger", "default", "delete",     "do",        "else",     "enum",   "export",
      "extends",  "false",   "finally",    "for",       "function", "if",     "implements",
      "import",   "in",      "instanceof", "interface", "let",      "new",    "null",
      "package",  "private", "protected",  "public",    "return",   "static", "super",
      "switch",   "this",    "throw",      "true",      "try",      "typeof", "var",
      "void",     "while",   "with",       "yield"};
  static const std::unordered_set<std::string> cpp{"alignas",      "alignof",
                                                   "and",          "and_eq",
                                                   "asm",          "auto",
                                                   "bitand",       "bitor",
                                                   "bool",         "break",
                                                   "case",         "catch",
                                                   "char",         "char16_t",
                                                   "char32_t",     "class",
                                                   "compl",        "const",
                                                   "constexpr",    "const_cast",
                                                   "continue",     "decltype",
                                                   "default",      "delete",
                                                   "do",           "double",
                                                   "dynamic_cast", "else",
                                                   "enum",         "explicit",
                                                   "export",       "extern",
                                                   "false",        "float",
                                                   "for",          "friend",
                                                   "goto",         "if",
                                                   "inline",       "int",
                                                   "long",         "mutable",
                                                   "namespace",    "new",
                                                   "noexcept",     "not",
                                                   "not_eq",       "nullptr",
                                                   "operator",     "or",
                                                   "or_eq",        "private",
                                                   "protected",    "public",
                                                   "register",     "reinterpret_cast",
                                                   "return",       "short",
                                                   "signed",       "sizeof",
                                                   "static",       "static_assert",
                                                   "static_cast",  "struct",
                                                   "switch",       "template",
                                                   "this",         "thread_local",
                                                   "throw",        "true",
                                                   "try",          "typedef",
                                                   "typeid",       "typename",
                                                   "union",        "unsigned",
                                                   "using",        "virtual",
                                                   "void",         "volatile",
                                                   "wchar_t",      "while",
                                                   "xor",          "xor_eq"};
  return (target == TargetLanguage::javascript ? javascript : cpp).count(name) != 0U;
}

std::string sanitize(std::string name) {
  for (auto& character : name) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte) == 0 && character != '_') character = '_';
  }
  if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front())) != 0) {
    name.insert(0, "mpf_");
  }
  return name;
}

std::string allocate_name(const TargetLanguage target, const std::string& original,
                          const std::set<std::string>& source_names, std::set<std::string>& used) {
  auto candidate = sanitize(original);
  const bool implementation_reserved =
      candidate.rfind("__mpf_", 0) == 0 ||
      (target == TargetLanguage::cpp &&
       (candidate.rfind("__", 0) == 0 ||
        (candidate.size() > 1 && candidate[0] == '_' &&
         std::isupper(static_cast<unsigned char>(candidate[1])) != 0)));
  if (reserved(candidate, target) || implementation_reserved) candidate.insert(0, "mpf_");
  const auto base = candidate;
  std::size_t suffix = 1;
  while (used.count(candidate) != 0U ||
         (candidate != original && source_names.count(candidate) != 0U)) {
    candidate = base + '_' + std::to_string(suffix++);
  }
  used.insert(candidate);
  return candidate;
}

}  // namespace

IdentifierPlan allocate_identifiers(const TargetLanguage target,
                                    const std::set<std::string>& originals) {
  IdentifierPlan plan;
  plan.target = target;
  for (const auto& original : originals) {
    plan.names.emplace(original, allocate_name(target, original, originals, plan.used));
  }
  return plan;
}

IdentifierPlan allocate_identifiers(const TargetLanguage target,
                                    const IdentifierInventory& originals) {
  IdentifierPlan plan;
  plan.target = target;
  plan.unique_symbol_names = target == TargetLanguage::cpp && originals.require_unique_symbol_names;
  if (!originals.valid) return plan;
  std::set<std::string> source_names = originals.names;
  for (const auto& [symbol, spelling] : originals.symbols) {
    (void)symbol;
    source_names.insert(spelling);
  }
  std::unordered_map<std::string, std::string> allocated;
  const auto target_name = [&](const std::string& spelling) -> const std::string& {
    const auto found = allocated.find(spelling);
    if (found != allocated.end()) return found->second;
    const auto inserted =
        allocated.emplace(spelling, allocate_name(target, spelling, source_names, plan.used));
    return inserted.first->second;
  };
  for (const auto& [symbol, spelling] : originals.symbols) {
    if (plan.unique_symbol_names) {
      plan.symbols.emplace(symbol, allocate_name(target, spelling, source_names, plan.used));
    } else {
      plan.symbols.emplace(symbol, target_name(spelling));
    }
  }
  for (const auto& spelling : originals.names) {
    plan.names.emplace(spelling, target_name(spelling));
  }
  return plan;
}

IdentifierMangler::IdentifierMangler(const TargetLanguage target,
                                     const std::set<std::string>& originals)
    : IdentifierMangler(allocate_identifiers(target, originals)) {}

IdentifierMangler::IdentifierMangler(const IdentifierPlan& plan)
    : names_(plan.names), symbols_(plan.symbols) {}

bool identifier_plan_complete(const IdentifierPlan& plan,
                              const std::set<std::string>& originals) noexcept {
  if (plan.names.size() != originals.size() || !plan.symbols.empty() ||
      plan.used.size() != originals.size()) {
    return false;
  }
  for (const auto& original : originals) {
    const auto found = plan.names.find(original);
    if (found == plan.names.end() || found->second.empty() ||
        plan.used.count(found->second) != 1U) {
      return false;
    }
  }
  return true;
}

bool identifier_plan_complete(const IdentifierPlan& plan,
                              const IdentifierInventory& originals) noexcept {
  if (!originals.valid) return false;
  std::set<std::string> source_names = originals.names;
  for (const auto& [symbol, spelling] : originals.symbols) {
    (void)symbol;
    source_names.insert(spelling);
  }
  if (plan.unique_symbol_names !=
      (plan.target == TargetLanguage::cpp && originals.require_unique_symbol_names)) {
    return false;
  }
  const auto expected_target_count = plan.unique_symbol_names
                                         ? originals.names.size() + originals.symbols.size()
                                         : source_names.size();
  if (plan.names.size() != originals.names.size() ||
      plan.symbols.size() != originals.symbols.size() ||
      plan.used.size() != expected_target_count) {
    return false;
  }
  std::unordered_map<std::string, std::string> targets_by_source;
  std::set<std::string> referenced_targets;
  const auto consistent = [&](const std::string& source, const std::string& target) {
    if (plan.unique_symbol_names) return referenced_targets.insert(target).second;
    const auto inserted = targets_by_source.emplace(source, target);
    if (!inserted.second && inserted.first->second != target) return false;
    referenced_targets.insert(target);
    return true;
  };
  for (const auto& name : originals.names) {
    const auto found = plan.names.find(name);
    if (found == plan.names.end() || found->second.empty() ||
        plan.used.count(found->second) != 1U || !consistent(name, found->second)) {
      return false;
    }
  }
  for (const auto& [symbol, spelling] : originals.symbols) {
    (void)spelling;
    const auto found = plan.symbols.find(symbol);
    if (found == plan.symbols.end() || found->second.empty() ||
        plan.used.count(found->second) != 1U || !consistent(spelling, found->second)) {
      return false;
    }
  }
  return referenced_targets == plan.used;
}

const std::string& IdentifierMangler::name(const std::string& source_name) const {
  const auto found = names_.find(source_name);
  return found == names_.end() ? source_name : found->second;
}

const std::string& IdentifierMangler::name(const SymbolId symbol,
                                           const std::string& source_name) const {
  if (symbol.valid()) {
    const auto found = symbols_.find(symbol);
    if (found != symbols_.end()) return found->second;
  }
  return name(source_name);
}

std::string reserve_internal_identifier(std::set<std::string>& used, const std::string& stem,
                                        const std::uint32_t node, const std::size_t ordinal) {
  const auto base =
      "mpf_internal_" + stem + '_' + std::to_string(node) + '_' + std::to_string(ordinal);
  auto candidate = base;
  std::size_t collision = 0;
  while (used.count(candidate) != 0U) {
    candidate = base + '_' + std::to_string(++collision);
  }
  used.insert(candidate);
  return candidate;
}

}  // namespace mpf::detail
