#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;

fs::path checked_destination(const fs::path& destination, const fs::path& build) {
  const auto corpus = fs::weakly_canonical(destination);
  const auto relative = corpus.lexically_relative(build);
  if (relative.empty() || relative == "." || relative.is_absolute() || *relative.begin() == "..")
    throw std::runtime_error(
        "Fuzz corpus output must be a directory beneath the project's root build/");
  return corpus;
}

void prepare(const fs::path& project, const fs::path& destination) {
  const auto root = fs::canonical(project);
  const auto build = fs::canonical(root / "build");
  const auto corpus = checked_destination(destination, build);
  constexpr std::array<const char*, 4> languages{"python", "matlab", "fortran", "typescript"};
  constexpr std::array<const char*, 2> targets{"javascript", "cpp"};
  std::size_t prepared = 0U;
  for (std::size_t language = 0U; language < languages.size(); ++language) {
    const auto seeds = root / "tests" / "fuzz" / "corpus" / languages[language];
    for (const auto& entry : fs::recursive_directory_iterator(seeds)) {
      if (entry.is_symlink()) throw std::runtime_error("Source fuzz seeds must not be symlinks");
      if (!entry.is_regular_file()) continue;
      std::ifstream input(entry.path(), std::ios::binary);
      if (!input) throw std::runtime_error("Cannot read fuzz seed");
      const std::string payload{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
      if (input.bad() ||
          payload.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("Cannot read complete fuzz seed");
      const auto seed = entry.path().lexically_relative(seeds);
      for (std::size_t target = 0U; target < targets.size(); ++target) {
        const auto output_path =
            checked_destination(corpus / languages[language] / targets[target] / seed, build);
        fs::create_directories(output_path.parent_path());
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        const std::array<char, 2> prefix{static_cast<char>(language + 4U),
                                         static_cast<char>(target + 2U)};
        output.write(prefix.data(), static_cast<std::streamsize>(prefix.size()));
        output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        output.close();
        if (!output) throw std::runtime_error("Cannot write framed fuzz seed");
        ++prepared;
      }
    }
  }
  if (prepared == 0U) throw std::runtime_error("No source seeds were found");
  std::cout << "Prepared " << prepared << " binary-framed fuzz seeds under " << corpus.u8string()
            << '\n';
}
}  // namespace

int main(const int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: mpf-fuzz-corpus-preparer <project-root> <corpus-output>\n";
    return 2;
  }
  try {
    prepare(fs::u8path(argv[1]), fs::u8path(argv[2]));
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Fuzz corpus preparation failed: " << error.what() << '\n';
    return 1;
  }
}
