# MPF

[![Build & Test](https://github.com/abandoft/mpf/actions/workflows/build-and-test.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/build-and-test.yml)
[![Platform Compatibility](https://github.com/abandoft/mpf/actions/workflows/platform-compatibility.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/platform-compatibility.yml)
[![Code Quality](https://github.com/abandoft/mpf/actions/workflows/code-quality.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/code-quality.yml)
[![Memory Safety](https://github.com/abandoft/mpf/actions/workflows/memory-safety.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/memory-safety.yml)
[![Test Coverage](https://github.com/abandoft/mpf/actions/workflows/test-coverage.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/test-coverage.yml)
[![Performance Regression](https://github.com/abandoft/mpf/actions/workflows/performance-regression.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/performance-regression.yml)
[![Security Analysis](https://github.com/abandoft/mpf/actions/workflows/security-analysis.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/security-analysis.yml)

A modern, high-performance multilingual transpilation framework. MPF converts supported source languages into modern JavaScript or portable C++17. The project provides both the `mpfc` command-line tool and a C++ library API, uses CMake for its build system, and allows the two output backends to be enabled independently.

## Features

- Supports Matlab, Python, Fortran, and TypeScript as input languages.
- Provides independent JavaScript and C++17 output backends.
- Detects languages from file extensions, with explicit selection available through the CLI or C++ API.
- Provides text and JSON diagnostics, source ranges, source maps, dependency manifests, and compilation reports.
- Generates JavaScript as ESM or strict scripts and produces directly compilable C++17 translation units.
- Supports backend-specific builds and install-package components.
- Supports GCC, Clang, AppleClang, and MSVC.

## Support Status

| Input language | Recognized extensions | Current capabilities |
|---|---|---|
| Matlab | `.m` | Scripts and local functions, conditionals/loops/scalar `switch`, element-wise N-D `~`/`&`/`|`, scalar short-circuit `&&`/`||`, array condition truthiness, shape-aware `all`/`any` logical reductions, real and complex scalars/arrays, shape-preserving `0×0` and zero-extent arrays, static N-D and local-function runtime-shape implicit expansion, array comparisons, static and runtime-sized `end`, ordered/repeated/empty numeric and linear/per-dimension logical selectors, vector/matrix/N-D growth and single-axis deletion, ordinary/conjugate transpose, sections, `reshape`, complex element-wise arithmetic, two-dimensional real/complex matrix multiplication, structure-aware real square solves, Hermitian-positive-definite/dense complex square solves, rank-aware real/complex rectangular solves, all R2024 `sparse` call forms within the non-empty static real rank-2 CSC contract, sparse scalar/linear/submatrix selection plus indexed assignment, growth, and deletion, sparse transpose/query/count and sparse square solves, safe-integer real/complex square matrix power, and multiple-output functions |
| Python | `.py`, `.pyw` | Functions and parameters, conditionals and loops, lists/tuples, unpacking, comparison chains, multidimensional arrays, indexing, and slicing |
| Fortran | `.f`, `.for`, `.ftn`, `.f77`, `.f90`, and others | Free/fixed form, functions/subroutines, `INTENT`/`OPTIONAL`, arrays and sections, and `SELECT CASE` |
| TypeScript | `.ts`, `.mts`, `.cts` | Typed scalars and arrays, functions, block scope, conditionals, `while`, and standard C-style `for` loops |

Language names are limited to `matlab`, `python`, `fortran`, and `typescript`; output targets are limited to `javascript` and `cpp`. `cpp` is the target name, and the current generated language standard is C++17.

See the [language support matrix](docs/LANGUAGE_SUPPORT.md) for the complete set of supported syntax, semantics, and limitations. See the [product plan](docs/MATLAB_TO_JAVASCRIPT.md) for the Matlab-to-JavaScript maturity analysis, completion criteria, and dedicated checklist, and the [project roadmap](TODO.md) for cross-language work.

## Requirements

- CMake 3.20 or later
- A compiler with C++17 support
- Node.js to run generated JavaScript or the complete differential test suite
- Python and gfortran to run the corresponding source-language differential tests

All build artifacts must remain under the repository's top-level `build/` directory.

## Building

Development build:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Release build:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Without presets:

```sh
cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure
```

## Command-Line Quick Start

Transpile Python to JavaScript and run it:

```sh
build/release/mpfc --target javascript examples/python/basic.py -o build/basic.mjs
node build/basic.mjs
```

Transpile Python to C++, compile it, and run it:

```sh
build/release/mpfc --target cpp examples/python/basic.py -o build/basic.cpp
c++ -std=c++17 build/basic.cpp -o build/basic
build/basic
```

Examples for other input languages:

```sh
build/release/mpfc --target javascript examples/matlab/basic.m -o build/matlab-basic.mjs
build/release/mpfc --target javascript examples/matlab/sparse_square_solve.m -o build/matlab-sparse.mjs
build/release/mpfc --target cpp examples/fortran/basic.f90 -o build/fortran-basic.cpp
build/release/mpfc --target javascript examples/typescript/basic.ts -o build/typescript-basic.mjs
```

Read fixed-form Fortran from standard input:

```sh
build/release/mpfc --language fortran --fortran-form fixed - < examples/fortran/fixed_form.f
```

Generate JSON diagnostics or a source map:

```sh
build/release/mpfc --diagnostics-format json --language python input.py
build/release/mpfc --target javascript --source-map build/output.mjs.map input.py -o build/output.mjs
```

List all command-line options:

```sh
build/release/mpfc --help
```

## Using MPF as a C++ Library

Install MPF:

```sh
cmake --install build/release --prefix build/stage
```

Find the exact current version in another project:

```cmake
find_package(mpf 0.6.3 EXACT CONFIG REQUIRED COMPONENTS core cpp)
target_link_libraries(my_application PRIVATE mpf::mpf)
```

Minimal example:

```cpp
#include <iostream>
#include <mpf/mpf.hpp>

int main() {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::python;
  options.target = mpf::TargetLanguage::cpp;
  options.filename = "example.py";

  const auto result = mpf::Transpiler{}.transpile("print(40 + 2)\n", options);
  if (!result.success()) {
    for (const auto& diagnostic : result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    return 1;
  }

  std::cout << result.code;
}
```

The installed package provides the `core`, `javascript`, and `cpp` components; the `mpf::core`, `mpf::backend-javascript`, and `mpf::backend-cpp` targets; and the unified `mpf::mpf` entry point. See [`examples/embedding`](examples/embedding) for a complete integration example; configure it with `-DMPF_REQUIRED_VERSION=0.6.3` so the consumer keeps exact-version matching.

MPF 0.x installs static libraries deliberately. A supported shared-library ABI will require an explicit symbol-export, allocator/ownership, and version-negotiation contract; setting `BUILD_SHARED_LIBS` does not silently expose the current internal C++ ABI.

## Build Configuration

| CMake option | Default | Purpose |
|---|---:|---|
| `MPF_BUILD_CLI` | `ON` | Build the `mpfc` command-line tool |
| `MPF_BUILD_TESTS` | `ON` for the top-level project | Build the test suite |
| `MPF_ENABLE_JAVASCRIPT_BACKEND` | `ON` | Build the JavaScript backend |
| `MPF_ENABLE_CPP_BACKEND` | `ON` | Build the C++ backend |
| `MPF_ENABLE_WERROR` | `OFF` | Treat compiler warnings as errors |
| `MPF_ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer |

For example, to build only the C++ backend:

```sh
cmake -S . -B build/cpp-only \
  -DMPF_ENABLE_JAVASCRIPT_BACKEND=OFF \
  -DMPF_ENABLE_CPP_BACKEND=ON
cmake --build build/cpp-only --parallel
```

## Documentation

- [Language support matrix](docs/LANGUAGE_SUPPORT.md)
- [Command line and diagnostics](docs/DIAGNOSTICS.md)
- [Installation, testing, and quality checks](docs/TESTING.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Adding an input language or output target](docs/EXTENDING.md)
- [Versioning policy](docs/VERSIONING.md)
- [Development roadmap](TODO.md)
- [Changelog](CHANGELOG.md)

## Contributing

Contributions to language support, output backends, tests, performance, and documentation are welcome through issues and pull requests. Before getting started, read the [contributing guide](docs/CONTRIBUTING.md) and ensure that every new language behavior includes success, boundary, and rejection cases.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```
