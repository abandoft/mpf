#include "frontend_registry.hpp"

#include <array>
#include <cctype>
#include <new>
#include <utility>

namespace mpf::detail {
namespace {

using FrontendFactory = const FrontendDescriptor& (*)() noexcept;

constexpr std::array<FrontendFactory, 3> factories{&matlab_frontend, &python_frontend,
                                                   &fortran_frontend};

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

bool has_name(const FrontendDescriptor& descriptor, const std::string_view name) noexcept {
  if (equals_ci(descriptor.name, name)) return true;
  for (std::size_t index = 0; index < descriptor.aliases.size; ++index) {
    if (equals_ci(descriptor.aliases.data[index], name)) return true;
  }
  return false;
}

std::string_view name_at(const FrontendDescriptor& descriptor, const std::size_t index) noexcept {
  return index == 0 ? std::string_view(descriptor.name) : descriptor.aliases.data[index - 1];
}

std::string_view extension_of(const std::string_view filename) noexcept {
  const auto separator = filename.find_last_of("/\\");
  const auto dot = filename.find_last_of('.');
  if (dot == std::string_view::npos || (separator != std::string_view::npos && dot < separator)) {
    return {};
  }
  return filename.substr(dot);
}

}  // namespace

FrontendParseResult parse_with_frontend(const FrontendDescriptor& descriptor,
                                        const SourceText& source,
                                        const FrontendParseOptions& options) {
  FrontendParseResult result;
  const auto error = [&](std::string message, std::string code = "MPF0009") {
    result.diagnostics.push_back(
        {DiagnosticSeverity::error, std::move(code), std::move(message), {1, 1}});
  };
  if (descriptor.create_parser_session == nullptr) {
    error("frontend descriptor has no parser session factory");
    return result;
  }
  if (!descriptor.manifest.features.contains(options.requested_features)) {
    error("frontend does not provide all requested parser features");
    return result;
  }
  if (options.memory_resource == nullptr) {
    error("frontend parser session requires a memory resource");
    return result;
  }
  if (source.content().size() > options.resource_limits.max_source_bytes) {
    error("resource limit exceeded at 'source-bytes': " + std::to_string(source.content().size()) +
              " exceeds " + std::to_string(options.resource_limits.max_source_bytes),
          "MPF0010");
    return result;
  }
  try {
    auto session = descriptor.create_parser_session(options);
    if (session == nullptr) {
      error("frontend parser session factory returned null");
      return result;
    }
    result = session->parse(source);
    const auto nodes = frontend_ast_node_count(result.ast);
    if (nodes > options.resource_limits.max_ast_nodes) {
      error("resource limit exceeded at 'ast-nodes': " + std::to_string(nodes) + " exceeds " +
                std::to_string(options.resource_limits.max_ast_nodes),
            "MPF0010");
    }
  } catch (const std::bad_alloc&) {
    error("resource limit exceeded at 'arena-bytes'", "MPF0010");
  }
  return result;
}

const FrontendDescriptor* find_frontend(const SourceLanguage language) noexcept {
  for (const auto factory : factories) {
    const auto* descriptor = &factory();
    if (descriptor->language == language) return descriptor;
  }
  return nullptr;
}

const FrontendDescriptor* find_frontend(const std::string_view name) noexcept {
  for (const auto factory : factories) {
    const auto* descriptor = &factory();
    if (has_name(*descriptor, name)) return descriptor;
  }
  return nullptr;
}

std::size_t frontend_count() noexcept {
  return factories.size();
}

const FrontendDescriptor* frontend_at(const std::size_t index) noexcept {
  return index < factories.size() ? &factories[index]() : nullptr;
}

const FrontendDescriptor* detect_frontend(const std::string_view source,
                                          const std::string_view filename) noexcept {
  const auto extension = extension_of(filename);
  const FrontendDescriptor* extension_match = nullptr;
  if (!extension.empty()) {
    for (const auto factory : factories) {
      const auto* descriptor = &factory();
      for (std::size_t index = 0; index < descriptor->extensions.size; ++index) {
        if (!equals_ci(descriptor->extensions.data[index], extension)) continue;
        if (extension_match != nullptr) return nullptr;
        extension_match = descriptor;
      }
    }
    if (extension_match != nullptr) return extension_match;
  }

  const FrontendDescriptor* best = nullptr;
  int best_score = 0;
  bool tied = false;
  for (const auto factory : factories) {
    const auto* descriptor = &factory();
    const auto score = descriptor->probe(source);
    if (score > best_score) {
      best = descriptor;
      best_score = score;
      tied = false;
    } else if (score != 0 && score == best_score) {
      tied = true;
    }
  }
  return tied ? nullptr : best;
}

bool validate_frontend_catalog(const FrontendDescriptor* const* descriptors,
                               const std::size_t count) noexcept {
  for (std::size_t left = 0; left < count; ++left) {
    const auto* descriptor = descriptors[left];
    if (descriptor == nullptr || descriptor->api_version != frontend_descriptor_api_version ||
        descriptor->language == SourceLanguage::automatic || descriptor->name == nullptr ||
        descriptor->name[0] == '\0' || descriptor->create_parser_session == nullptr ||
        descriptor->verify == nullptr || descriptor->lower == nullptr ||
        descriptor->probe == nullptr || descriptor->manifest.language_version == nullptr ||
        descriptor->manifest.language_version[0] == '\0' ||
        descriptor->manifest.ast_schema == nullptr || descriptor->manifest.ast_schema[0] == '\0' ||
        descriptor->manifest.minimum_version.automatic() ||
        descriptor->manifest.maximum_version.automatic() ||
        descriptor->manifest.maximum_version < descriptor->manifest.minimum_version ||
        !descriptor->manifest.features.contains(FrontendFeature::language_versioning) ||
        descriptor->manifest.resource_contract.bits() !=
            standard_frontend_resource_contract.bits() ||
        !descriptor->manifest.deterministic || !descriptor->manifest.reentrant ||
        (descriptor->aliases.size != 0 && descriptor->aliases.data == nullptr) ||
        (descriptor->extensions.size != 0 && descriptor->extensions.data == nullptr) ||
        (descriptor->intrinsic_table_count != 0 && descriptor->intrinsic_tables == nullptr)) {
      return false;
    }
    for (std::size_t first = 0; first <= descriptor->aliases.size; ++first) {
      if (name_at(*descriptor, first).empty()) return false;
      for (std::size_t second = first + 1; second <= descriptor->aliases.size; ++second) {
        if (equals_ci(name_at(*descriptor, first), name_at(*descriptor, second))) return false;
      }
    }
    for (std::size_t index = 0; index < descriptor->extensions.size; ++index) {
      const auto extension = descriptor->extensions.data[index];
      if (extension.size() < 2 || extension.front() != '.') return false;
      for (std::size_t other = index + 1; other < descriptor->extensions.size; ++other) {
        if (equals_ci(extension, descriptor->extensions.data[other])) return false;
      }
    }
    for (std::size_t table_index = 0; table_index < descriptor->intrinsic_table_count;
         ++table_index) {
      const auto& table = descriptor->intrinsic_tables[table_index];
      if (table.size == 0 || table.data == nullptr) return false;
      for (std::size_t index = 0; index < table.size; ++index) {
        const auto& binding = table.data[index];
        if (binding.spelling.empty() || binding.intrinsic == IntrinsicId::none ||
            static_cast<std::size_t>(binding.intrinsic) >=
                static_cast<std::size_t>(IntrinsicId::count)) {
          return false;
        }
        if (index != 0 && table.data[index - 1].spelling >= binding.spelling) return false;
        for (std::size_t prior = 0; prior < table_index; ++prior) {
          const auto& prior_table = descriptor->intrinsic_tables[prior];
          if (lookup_source_intrinsic(prior_table.data, prior_table.size, binding.spelling) !=
              IntrinsicId::none) {
            return false;
          }
        }
      }
    }
    for (std::size_t right = left + 1; right < count; ++right) {
      const auto* other = descriptors[right];
      if (other == nullptr || descriptor->language == other->language) return false;
      for (std::size_t first = 0; first <= descriptor->aliases.size; ++first) {
        for (std::size_t second = 0; second <= other->aliases.size; ++second) {
          if (equals_ci(name_at(*descriptor, first), name_at(*other, second))) return false;
        }
      }
      for (std::size_t first = 0; first < descriptor->extensions.size; ++first) {
        for (std::size_t second = 0; second < other->extensions.size; ++second) {
          if (equals_ci(descriptor->extensions.data[first], other->extensions.data[second])) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

}  // namespace mpf::detail
