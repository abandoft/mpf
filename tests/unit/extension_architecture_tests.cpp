#include <array>
#include <string>
#include <utility>

#include "backends/cpp_bindings.hpp"
#include "backends/javascript_bindings.hpp"
#include "compiler/code_binding.hpp"
#include "compiler/intrinsic.hpp"
#include "core/backend_registry.hpp"
#include "frontends/frontend_registry.hpp"
#include "test_framework.hpp"

namespace {

const mpf::detail::CodeBinding* missing_binding(mpf::detail::IntrinsicId) noexcept {
  return nullptr;
}

const mpf::detail::TargetProfile& wrong_target_profile() noexcept {
  static const mpf::detail::TargetProfile profile{mpf::TargetLanguage::cpp, "C++17", false, true,
                                                  false};
  return profile;
}

}  // namespace

TEST_CASE("frontend registry owns aliases extensions probes and parser callbacks") {
  const mpf::detail::FrontendDescriptor* descriptors[]{&mpf::detail::matlab_frontend(),
                                                       &mpf::detail::python_frontend(),
                                                       &mpf::detail::fortran_frontend()};
  REQUIRE(mpf::detail::validate_frontend_catalog(descriptors, std::size(descriptors)));
  REQUIRE(mpf::detail::find_frontend("PY") == &mpf::detail::python_frontend());
  REQUIRE(mpf::detail::detect_frontend("value = 1\n", "CALCULATION.PY") ==
          &mpf::detail::python_frontend());
  REQUIRE(mpf::detail::detect_frontend("implicit none\n", "") == &mpf::detail::fortran_frontend());
  REQUIRE(mpf::detail::detect_frontend("import values\ndisp(values)\n", "") == nullptr);
}

TEST_CASE("frontend catalog rejects duplicate registrations") {
  const mpf::detail::FrontendDescriptor* duplicate[]{&mpf::detail::python_frontend(),
                                                     &mpf::detail::python_frontend()};
  REQUIRE(!mpf::detail::validate_frontend_catalog(duplicate, std::size(duplicate)));

  auto malformed = mpf::detail::python_frontend();
  malformed.lower = nullptr;
  const mpf::detail::FrontendDescriptor* missing_lowering[]{&malformed};
  REQUIRE(!mpf::detail::validate_frontend_catalog(missing_lowering, std::size(missing_lowering)));
  malformed = mpf::detail::python_frontend();
  malformed.verify = nullptr;
  const mpf::detail::FrontendDescriptor* missing_verifier[]{&malformed};
  REQUIRE(!mpf::detail::validate_frontend_catalog(missing_verifier, std::size(missing_verifier)));
  malformed = mpf::detail::python_frontend();
  malformed.manifest.deterministic = false;
  const mpf::detail::FrontendDescriptor* nondeterministic[]{&malformed};
  REQUIRE(!mpf::detail::validate_frontend_catalog(nondeterministic, std::size(nondeterministic)));
  malformed = mpf::detail::python_frontend();
  const mpf::detail::SourceIntrinsicBinding unsorted[]{
      {"len", mpf::detail::IntrinsicId::python_length},
      {"float", mpf::detail::IntrinsicId::python_float}};
  const mpf::detail::SourceIntrinsicTable malformed_tables[]{{unsorted, std::size(unsorted)}};
  malformed.intrinsic_tables = malformed_tables;
  malformed.intrinsic_table_count = std::size(malformed_tables);
  const mpf::detail::FrontendDescriptor* malformed_catalog[]{&malformed};
  REQUIRE(!mpf::detail::validate_frontend_catalog(malformed_catalog, std::size(malformed_catalog)));

  const mpf::detail::SourceIntrinsicBinding repeated[]{{"abs", mpf::detail::IntrinsicId::absolute}};
  const mpf::detail::SourceIntrinsicTable repeated_tables[]{
      mpf::detail::mathematical_intrinsic_table(), {repeated, std::size(repeated)}};
  malformed.intrinsic_tables = repeated_tables;
  malformed.intrinsic_table_count = std::size(repeated_tables);
  REQUIRE(!mpf::detail::validate_frontend_catalog(malformed_catalog, std::size(malformed_catalog)));
}

TEST_CASE("backend registry owns target aliases availability and callback contracts") {
  const mpf::detail::BackendDescriptor* descriptors[]{
      mpf::detail::find_backend(mpf::TargetLanguage::javascript),
      mpf::detail::find_backend(mpf::TargetLanguage::cpp)};
  REQUIRE(mpf::detail::validate_backend_catalog(descriptors, std::size(descriptors)));
  REQUIRE(mpf::detail::find_backend_descriptor("JS")->target == mpf::TargetLanguage::javascript);
  REQUIRE(mpf::detail::find_backend_descriptor("c++")->target == mpf::TargetLanguage::cpp);
  const mpf::detail::BackendDescriptor* duplicate[]{descriptors[0], descriptors[0]};
  REQUIRE(!mpf::detail::validate_backend_catalog(duplicate, std::size(duplicate)));

  auto malformed = *descriptors[0];
  malformed.profile = &wrong_target_profile;
  const mpf::detail::BackendDescriptor* wrong_profile[]{&malformed};
  REQUIRE(!mpf::detail::validate_backend_catalog(wrong_profile, std::size(wrong_profile)));
  malformed = *descriptors[0];
  malformed.manifest.reentrant = false;
  const mpf::detail::BackendDescriptor* nonreentrant[]{&malformed};
  REQUIRE(!mpf::detail::validate_backend_catalog(nonreentrant, std::size(nonreentrant)));
}

TEST_CASE("source intrinsic catalog is language scoped and uses stable identities") {
  using mpf::detail::IntrinsicId;
  REQUIRE(mpf::detail::find_intrinsic(mpf::SourceLanguage::python, "len") ==
          IntrinsicId::python_length);
  REQUIRE(mpf::detail::find_intrinsic(mpf::SourceLanguage::matlab, "len") == IntrinsicId::none);
  REQUIRE(mpf::detail::find_intrinsic(mpf::SourceLanguage::matlab, "numel") ==
          IntrinsicId::element_count);
  REQUIRE(mpf::detail::find_intrinsic(mpf::SourceLanguage::fortran, "size") ==
          IntrinsicId::element_count);
  REQUIRE(mpf::detail::find_intrinsic(mpf::SourceLanguage::automatic, "abs") == IntrinsicId::none);
  REQUIRE(mpf::detail::intrinsic_count() == static_cast<std::size_t>(IntrinsicId::count));
}

TEST_CASE("every intrinsic has an explicit JavaScript and cpp code binding") {
  using mpf::detail::CodeBindingKind;
  using mpf::detail::IntrinsicId;
  for (std::size_t index = 1; index < static_cast<std::size_t>(IntrinsicId::count); ++index) {
    const auto intrinsic = static_cast<IntrinsicId>(index);
    const auto* javascript = mpf::detail::javascript_code_binding(intrinsic);
    const auto* cpp = mpf::detail::cpp_code_binding(intrinsic);
    REQUIRE(javascript != nullptr);
    REQUIRE(cpp != nullptr);
    REQUIRE(javascript->kind != CodeBindingKind::unavailable);
    REQUIRE(cpp->kind != CodeBindingKind::unavailable);
  }
}

TEST_CASE("missing target code bindings fail closed before emission") {
  mpf::detail::Expression expression;
  expression.kind = mpf::detail::ExpressionKind::identifier;
  expression.binding = mpf::detail::BindingKind::builtin;
  expression.intrinsic = mpf::detail::IntrinsicId::square_root;
  expression.location = {7, 3};

  mpf::detail::Statement statement;
  statement.kind = mpf::detail::StatementKind::expression;
  statement.has_expression = true;
  statement.expression = std::move(expression);

  mpf::detail::Program program;
  program.language = mpf::SourceLanguage::python;
  program.statements.push_back(std::move(statement));
  const auto diagnostics =
      mpf::detail::validate_code_bindings(program, &missing_binding, "test-target");
  REQUIRE(diagnostics.size() == 1);
  REQUIRE(diagnostics.front().code == "MPF0004");
  REQUIRE(diagnostics.front().location.line == 7);
  REQUIRE(diagnostics.front().message.find("square_root") != std::string::npos);
}
