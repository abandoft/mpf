#pragma once

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <string_view>

#include "../compiler/descriptor.hpp"
#include "frontend_ast.hpp"

namespace mpf::detail {

inline constexpr std::uint32_t frontend_descriptor_api_version = 4;

struct FrontendManifest {
  const char* language_version{"unknown"};
  const char* ast_schema{"unknown"};
  LanguageVersion minimum_version{};
  LanguageVersion maximum_version{};
  bool deterministic{false};
  bool reentrant{false};
};

struct FrontendParseOptions {
  FortranSourceForm fortran_source_form{FortranSourceForm::automatic};
  LanguageVersion language_version{};
  std::pmr::memory_resource* memory_resource{std::pmr::get_default_resource()};
};

struct FrontendDescriptor {
  std::uint32_t api_version{frontend_descriptor_api_version};
  SourceLanguage language{SourceLanguage::automatic};
  const char* name{"unknown"};
  StringViewList aliases;
  StringViewList extensions;
  FrontendManifest manifest;
  const SourceIntrinsicTable* intrinsic_tables{nullptr};
  std::size_t intrinsic_table_count{0};
  FrontendParseResult (*parse)(const SourceText& source,
                               const FrontendParseOptions& options){nullptr};
  std::vector<Diagnostic> (*verify)(const FrontendAst& ast){nullptr};
  hir::LoweringResult (*lower)(FrontendAst&& ast){nullptr};
  int (*probe)(std::string_view source) noexcept {nullptr};
};

[[nodiscard]] const FrontendDescriptor& python_frontend() noexcept;
[[nodiscard]] const FrontendDescriptor& matlab_frontend() noexcept;
[[nodiscard]] const FrontendDescriptor& fortran_frontend() noexcept;

}  // namespace mpf::detail
