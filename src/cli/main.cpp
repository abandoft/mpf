#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mpf/transpiler.hpp"
#include "mpf/version.hpp"

namespace {

enum class DiagnosticFormat { text, json };

enum class ExitCode : int {
  success = 0,
  compilation_error = 1,
  command_line_error = 2,
  input_error = 3,
  output_error = 4
};

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

void usage(std::ostream& output) {
  std::string source_languages{"auto"};
  for (const auto language : mpf::registered_source_languages()) {
    source_languages += '|';
    source_languages += mpf::to_string(language);
  }
  std::string target_languages;
  for (const auto language : mpf::registered_target_languages()) {
    if (!target_languages.empty()) target_languages += '|';
    target_languages += mpf::to_string(language);
  }
  output << "Usage: mpfc [options] <input|->\n"
            "Options:\n"
            "  -l, --language <"
         << source_languages << ">\n"
         << "  -t, --target <" << target_languages
         << ">\n"
            "      --language-version <latest|major[.minor]|Ryyyy[a|b]>\n"
            "  -m, --module <esm|script>\n"
            "      --fortran-form <auto|free|fixed>\n"
            "  -o, --output <path>\n"
            "      --source-map <path>\n"
            "      --diagnostics-format <text|json>\n"
            "      --no-banner\n"
            "  -h, --help\n"
            "  -v, --version\n"
            "Exit status: 0 success, 1 compilation error, 2 command-line error,\n"
            "             3 input error, 4 output error.\n";
}

bool read_all(const std::string& path, std::string& content) {
  if (path == "-") {
    content.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    return !std::cin.bad();
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  return !input.bad();
}

DiagnosticFormat discover_diagnostic_format(const int argc, char** argv) {
  auto format = DiagnosticFormat::text;
  for (int index = 1; index + 1 < argc; ++index) {
    if (std::string(argv[index]) != "--diagnostics-format") continue;
    const auto value = lowercase(argv[index + 1]);
    if (value == "json") format = DiagnosticFormat::json;
    if (value == "text") format = DiagnosticFormat::text;
  }
  return format;
}

mpf::Diagnostic make_driver_diagnostic(std::string code, std::string message,
                                       std::string source_name = "<command-line>") {
  mpf::Diagnostic diagnostic{
      mpf::DiagnosticSeverity::error, std::move(code), std::move(message), {1, 1}};
  diagnostic.source_name = std::move(source_name);
  return diagnostic;
}

void emit_diagnostics(const DiagnosticFormat format,
                      const std::vector<mpf::Diagnostic>& diagnostics,
                      const std::string_view source = {}, const std::string_view source_name = {}) {
  if (format == DiagnosticFormat::json) {
    std::cerr << mpf::render_diagnostics_json(diagnostics) << '\n';
  } else {
    for (const auto& diagnostic : diagnostics) {
      mpf::DiagnosticRenderOptions render_options;
      render_options.show_source = !source.empty() && diagnostic.source_name == source_name;
      std::cerr << mpf::render_diagnostic_text(diagnostic, source, render_options);
    }
  }
}

int report_driver_error(const DiagnosticFormat format, std::string code, std::string message,
                        const ExitCode exit_code, std::string source_name = "<command-line>") {
  emit_diagnostics(format, {make_driver_diagnostic(std::move(code), std::move(message),
                                                   std::move(source_name))});
  return static_cast<int>(exit_code);
}

}  // namespace

int main(int argc, char** argv) {
  mpf::TranspileOptions options;
  std::string input_path;
  std::string output_path;
  std::string source_map_path;
  auto diagnostic_format = discover_diagnostic_format(argc, argv);

  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "-h" || argument == "--help") {
      usage(std::cout);
      return static_cast<int>(ExitCode::success);
    }
    if (argument == "-v" || argument == "--version") {
      std::cout << "mpfc " << MPF_VERSION_STRING << '\n';
      return static_cast<int>(ExitCode::success);
    }
    if ((argument == "-l" || argument == "--language") && index + 1 < argc) {
      const std::string value = argv[++index];
      const auto language = mpf::parse_source_language(value);
      if (!language.has_value()) {
        return report_driver_error(diagnostic_format, "MPFCLI0001",
                                   "unknown source language: " + value,
                                   ExitCode::command_line_error);
      }
      options.language = *language;
      continue;
    }
    if ((argument == "-m" || argument == "--module") && index + 1 < argc) {
      const std::string value = argv[++index];
      if (value == "esm") {
        options.module_kind = mpf::ModuleKind::esm;
      } else if (value == "script") {
        options.module_kind = mpf::ModuleKind::script;
      } else {
        return report_driver_error(diagnostic_format, "MPFCLI0001", "unknown module kind: " + value,
                                   ExitCode::command_line_error);
      }
      continue;
    }
    if (argument == "--language-version" && index + 1 < argc) {
      const std::string value = argv[++index];
      const auto version = mpf::parse_language_version(value);
      if (!version.has_value()) {
        return report_driver_error(diagnostic_format, "MPFCLI0001",
                                   "invalid language version: " + value,
                                   ExitCode::command_line_error);
      }
      options.language_version = *version;
      continue;
    }
    if ((argument == "-t" || argument == "--target") && index + 1 < argc) {
      const std::string value = argv[++index];
      const auto target = mpf::parse_target_language(value);
      if (!target.has_value()) {
        return report_driver_error(diagnostic_format, "MPFCLI0001",
                                   "unknown target language: " + value,
                                   ExitCode::command_line_error);
      }
      options.target = *target;
      continue;
    }
    if (argument == "--fortran-form" && index + 1 < argc) {
      const std::string value = argv[++index];
      const auto source_form = mpf::parse_fortran_source_form(value);
      if (!source_form.has_value()) {
        return report_driver_error(diagnostic_format, "MPFCLI0001",
                                   "unknown Fortran source form: " + value,
                                   ExitCode::command_line_error);
      }
      options.fortran_source_form = *source_form;
      continue;
    }
    if ((argument == "-o" || argument == "--output") && index + 1 < argc) {
      output_path = argv[++index];
      continue;
    }
    if (argument == "--source-map" && index + 1 < argc) {
      source_map_path = argv[++index];
      if (source_map_path == "-") {
        return report_driver_error(diagnostic_format, "MPFCLI0001",
                                   "source map path cannot be stdout",
                                   ExitCode::command_line_error);
      }
      continue;
    }
    if (argument == "--diagnostics-format" && index + 1 < argc) {
      const auto value = lowercase(argv[++index]);
      if (value == "text") {
        diagnostic_format = DiagnosticFormat::text;
      } else if (value == "json") {
        diagnostic_format = DiagnosticFormat::json;
      } else {
        return report_driver_error(diagnostic_format, "MPFCLI0001",
                                   "unknown diagnostics format: " + value,
                                   ExitCode::command_line_error);
      }
      continue;
    }
    if (argument == "--no-banner") {
      options.emit_source_banner = false;
      continue;
    }
    if (!argument.empty() && argument.front() == '-' && argument != "-") {
      return report_driver_error(diagnostic_format, "MPFCLI0001",
                                 "unknown or incomplete option: " + argument,
                                 ExitCode::command_line_error);
    }
    if (!input_path.empty()) {
      return report_driver_error(diagnostic_format, "MPFCLI0001", "only one input file is accepted",
                                 ExitCode::command_line_error);
    }
    input_path = argument;
  }

  if (input_path.empty()) {
    return report_driver_error(diagnostic_format, "MPFCLI0001", "an input file or '-' is required",
                               ExitCode::command_line_error);
  }
  std::string source;
  if (!read_all(input_path, source)) {
    return report_driver_error(diagnostic_format, "MPFCLI0002", "cannot read input: " + input_path,
                               ExitCode::input_error, input_path);
  }
  options.filename = input_path == "-" ? std::string("<stdin>") : input_path;
  if (!output_path.empty() && output_path != "-") options.generated_filename = output_path;
  auto result = mpf::Transpiler{}.transpile(source, options);
  if (!result.success()) {
    emit_diagnostics(diagnostic_format, result.diagnostics, source, options.filename);
    return static_cast<int>(ExitCode::compilation_error);
  }
  if (!source_map_path.empty()) {
    std::ofstream source_map_output(source_map_path, std::ios::binary);
    source_map_output << result.source_map.to_json();
    source_map_output.close();
    if (!source_map_output) {
      result.diagnostics.push_back(make_driver_diagnostic(
          "MPFCLI0003", "cannot write source map: " + source_map_path, source_map_path));
      emit_diagnostics(diagnostic_format, result.diagnostics, source, options.filename);
      return static_cast<int>(ExitCode::output_error);
    }
  }
  if (output_path.empty() || output_path == "-") {
    std::cout << result.code;
    std::cout.flush();
    if (!std::cout.good()) {
      result.diagnostics.push_back(make_driver_diagnostic(
          "MPFCLI0003", "cannot write generated output to stdout", "<stdout>"));
      emit_diagnostics(diagnostic_format, result.diagnostics, source, options.filename);
      return static_cast<int>(ExitCode::output_error);
    }
    emit_diagnostics(diagnostic_format, result.diagnostics, source, options.filename);
    return static_cast<int>(ExitCode::success);
  }
  std::ofstream output(output_path, std::ios::binary);
  output << result.code;
  output.close();
  if (!output) {
    result.diagnostics.push_back(
        make_driver_diagnostic("MPFCLI0003", "cannot write output: " + output_path, output_path));
    emit_diagnostics(diagnostic_format, result.diagnostics, source, options.filename);
    return static_cast<int>(ExitCode::output_error);
  }
  emit_diagnostics(diagnostic_format, result.diagnostics, source, options.filename);
  return static_cast<int>(ExitCode::success);
}
