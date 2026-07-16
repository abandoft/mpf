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

}  // namespace

IdentifierPlan allocate_identifiers(const TargetLanguage target,
                                    const std::set<std::string>& originals) {
  IdentifierPlan plan;
  for (const auto& original : originals) {
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
    while (plan.used.count(candidate) != 0U ||
           (candidate != original && originals.count(candidate) != 0U)) {
      candidate = base + '_' + std::to_string(suffix++);
    }
    plan.used.insert(candidate);
    plan.names.emplace(original, std::move(candidate));
  }
  return plan;
}

IdentifierMangler::IdentifierMangler(const TargetLanguage target,
                                     const std::set<std::string>& originals)
    : IdentifierMangler(allocate_identifiers(target, originals)) {}

IdentifierMangler::IdentifierMangler(const IdentifierPlan& plan)
    : names_(plan.names), used_(plan.used) {}

bool identifier_plan_complete(const IdentifierPlan& plan,
                              const std::set<std::string>& originals) noexcept {
  if (plan.names.size() != originals.size() || plan.used.size() != originals.size()) {
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

const std::string& IdentifierMangler::name(const std::string& source_name) const {
  const auto found = names_.find(source_name);
  return found == names_.end() ? source_name : found->second;
}

std::string IdentifierMangler::temporary(const std::string& stem) {
  std::string candidate;
  do {
    candidate = "mpf_internal_" + stem + '_' + std::to_string(temporary_index_++);
  } while (used_.count(candidate) != 0U);
  used_.insert(candidate);
  return candidate;
}

}  // namespace mpf::detail
