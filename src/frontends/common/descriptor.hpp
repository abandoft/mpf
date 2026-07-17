#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <string_view>

#include "compiler/static_string_list.hpp"
#include "frontends/common/ast.hpp"
#include "source/source_text.hpp"

namespace mpf::detail {

inline constexpr std::uint32_t frontend_descriptor_api_version = 6;

enum class FrontendFeature : std::uint64_t {
  language_versioning = 1ULL << 0U,
  structured_control_flow = 1ULL << 1U,
  functions = 1ULL << 2U,
  keyword_arguments = 1ULL << 3U,
  multiple_results = 1ULL << 4U,
  array_sections = 1ULL << 5U,
  fixed_source_form = 1ULL << 6U,
  parameter_intent = 1ULL << 7U
};

class FrontendFeatureSet final {
 public:
  constexpr FrontendFeatureSet() noexcept = default;
  explicit constexpr FrontendFeatureSet(const std::uint64_t bits) noexcept : bits_(bits) {}

  constexpr void add(const FrontendFeature feature) noexcept {
    bits_ |= static_cast<std::uint64_t>(feature);
  }
  [[nodiscard]] constexpr bool contains(const FrontendFeature feature) const noexcept {
    return (bits_ & static_cast<std::uint64_t>(feature)) != 0;
  }
  [[nodiscard]] constexpr bool contains(const FrontendFeatureSet requested) const noexcept {
    return (bits_ & requested.bits_) == requested.bits_;
  }
  [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0; }
  [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return bits_; }

 private:
  std::uint64_t bits_{0};
};

enum class FrontendResourceLimit : std::uint32_t {
  source_bytes = 1U << 0U,
  tokens = 1U << 1U,
  parser_depth = 1U << 2U,
  arena_bytes = 1U << 3U,
  ast_nodes = 1U << 4U
};

class FrontendResourceContract final {
 public:
  constexpr FrontendResourceContract() noexcept = default;
  explicit constexpr FrontendResourceContract(const std::uint32_t bits) noexcept : bits_(bits) {}

  [[nodiscard]] constexpr bool contains(const FrontendResourceLimit limit) const noexcept {
    return (bits_ & static_cast<std::uint32_t>(limit)) != 0;
  }
  [[nodiscard]] constexpr std::uint32_t bits() const noexcept { return bits_; }

 private:
  std::uint32_t bits_{0};
};

inline constexpr FrontendResourceContract standard_frontend_resource_contract{
    static_cast<std::uint32_t>(FrontendResourceLimit::source_bytes) |
    static_cast<std::uint32_t>(FrontendResourceLimit::tokens) |
    static_cast<std::uint32_t>(FrontendResourceLimit::parser_depth) |
    static_cast<std::uint32_t>(FrontendResourceLimit::arena_bytes) |
    static_cast<std::uint32_t>(FrontendResourceLimit::ast_nodes)};

struct FrontendManifest {
  const char* language_version{"unknown"};
  const char* ast_schema{"unknown"};
  LanguageVersion minimum_version{};
  LanguageVersion maximum_version{};
  FrontendFeatureSet features{};
  FrontendResourceContract resource_contract{};
  bool deterministic{false};
  bool reentrant{false};
};

struct FrontendParseOptions {
  FortranSourceForm fortran_source_form{FortranSourceForm::automatic};
  LanguageVersion language_version{};
  std::pmr::memory_resource* memory_resource{std::pmr::get_default_resource()};
  ResourceLimits resource_limits{};
  FrontendFeatureSet requested_features{};
};

class FrontendParserSession {
 public:
  FrontendParserSession() = default;
  virtual ~FrontendParserSession() = default;
  FrontendParserSession(const FrontendParserSession&) = delete;
  FrontendParserSession& operator=(const FrontendParserSession&) = delete;
  FrontendParserSession(FrontendParserSession&&) = delete;
  FrontendParserSession& operator=(FrontendParserSession&&) = delete;

  [[nodiscard]] virtual FrontendParseResult parse(const SourceText& source) = 0;
};

struct FrontendDescriptor {
  std::uint32_t api_version{frontend_descriptor_api_version};
  SourceLanguage language{SourceLanguage::automatic};
  const char* name{"unknown"};
  StaticStringList extensions;
  FrontendManifest manifest;
  const SourceIntrinsicTable* intrinsic_tables{nullptr};
  std::size_t intrinsic_table_count{0};
  std::unique_ptr<FrontendParserSession> (*create_parser_session)(
      const FrontendParseOptions& options){nullptr};
  std::vector<Diagnostic> (*verify)(const FrontendAst& ast){nullptr};
  hir::LoweringResult (*lower)(FrontendAst&& ast){nullptr};
  int (*probe)(std::string_view source) noexcept {nullptr};
};

[[nodiscard]] FrontendParseResult parse_with_frontend(const FrontendDescriptor& descriptor,
                                                      const SourceText& source,
                                                      const FrontendParseOptions& options = {});

[[nodiscard]] const FrontendDescriptor& python_frontend() noexcept;
[[nodiscard]] const FrontendDescriptor& matlab_frontend() noexcept;
[[nodiscard]] const FrontendDescriptor& fortran_frontend() noexcept;
[[nodiscard]] const FrontendDescriptor& typescript_frontend() noexcept;

}  // namespace mpf::detail
