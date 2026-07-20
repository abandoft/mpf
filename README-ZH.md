# MPF

[![构建与测试](https://github.com/abandoft/mpf/actions/workflows/build-and-test.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/build-and-test.yml)
[![平台兼容](https://github.com/abandoft/mpf/actions/workflows/platform-compatibility.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/platform-compatibility.yml)
[![代码质量](https://github.com/abandoft/mpf/actions/workflows/code-quality.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/code-quality.yml)
[![内存安全](https://github.com/abandoft/mpf/actions/workflows/memory-safety.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/memory-safety.yml)
[![测试覆盖率](https://github.com/abandoft/mpf/actions/workflows/test-coverage.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/test-coverage.yml)
[![性能回归](https://github.com/abandoft/mpf/actions/workflows/performance-regression.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/performance-regression.yml)
[![安全分析](https://github.com/abandoft/mpf/actions/workflows/security-analysis.yml/badge.svg?branch=main)](https://github.com/abandoft/mpf/actions/workflows/security-analysis.yml)

现代化、高性能的多语言转译框架。MPF 可以把受支持的源语言代码转换为现代 JavaScript 或可移植的 C++17。项目同时提供 `mpfc` 命令行工具和 C++ 库接口，使用 CMake 构建，两个输出后端可以独立启用。

## 功能

- 支持 Matlab、Python、Fortran 和 TypeScript 四种输入语言。
- 支持 JavaScript 和 C++17 两个彼此独立的输出后端。
- 根据文件扩展名自动识别语言，也可通过 CLI 或 C++ API 显式指定。
- 提供文本和 JSON 诊断、源码范围、source map、依赖清单和编译报告。
- 支持 ESM/strict script JavaScript 输出以及可直接编译的 C++17 translation unit。
- 支持按后端裁剪构建和安装包组件。
- 支持 GCC、Clang、AppleClang 和 MSVC。

## 支持范围

| 输入语言 | 自动识别扩展名 | 当前能力摘要 |
|---|---|---|
| Matlab | `.m` | 脚本与局部函数、条件/循环/标量 `switch`、N 维逐元素 `~`/`&`/`|`、标量短路 `&&`/`||`、数组 condition truthiness、保持 shape 的 `all`/`any` 逻辑归约、实数与复数标量/数组、保持 shape 的 `0×0` 与零 extent 数组、静态 N 维及 local-function runtime-shape 隐式扩展、数组比较、静态及运行时 extent 的 `end`、保序/重复/空 numeric selector、线性/逐维 logical selector、vector/matrix/N 维扩容与单轴删除、普通/共轭转置、section、`reshape`、复数逐元素算术、二维实数/复数矩阵乘法、结构感知实数方阵求解、Hermitian 正定/稠密复数方阵求解、rank-aware 实数/复数矩形求解、R2024 全部 `sparse` 调用形式对静态 rank-2 输入的实现，其中支持 real/logical value 以及 complex dense/triplet value（全零构造仍返回 double）、numeric/complex 重复项求和与 logical 重复项 `any`，以及保持类型的稀疏 scalar/linear/submatrix selection、indexed assignment、扩容、删除、reshape、转置/`full`/查询/计数、含 `0×0` 系数及 shaped-empty 结果的稀疏方阵求解，以及保持 Matlab 结果 storage 的 finite-real sparse×sparse/sparse×dense/dense×sparse 矩阵乘法、双向 sparse/scalar 缩放、compatible-size sparse `.*` scalar/dense/sparse 乘法、sparse-sparse 保持类型化 CSC 且 mixed 物化 dense 的 compatible-size real/logical/complex sparse `+`/`-`、静态 rank-2 sparse logical `~`/`&`/`|`（`~S`、稀疏 AND 与 sparse-sparse OR 保持 CSC，mixed OR 物化 dense）、静态 real/logical rank-2 sparse `all`/`any`（非标量结果保持 logical CSC，全维结果为 full logical scalar）、保持 CSC 的 real/logical 稀疏方阵非负 safe-integer 幂、稠密实数/复数方阵 safe-integer 整数幂与多输出函数 |
| Python | `.py`、`.pyw` | 函数与参数、条件和循环、list/tuple、解包、比较链、多维数组、索引和切片 |
| Fortran | `.f`、`.for`、`.ftn`、`.f77`、`.f90` 等 | free/fixed form、function/subroutine、`INTENT`/`OPTIONAL`、数组与 section、`SELECT CASE` |
| TypeScript | `.ts`、`.mts`、`.cts` | 类型化标量和数组、函数、块作用域、条件、`while`、标准 C 风格 `for` |

语言名称只接受 `matlab`、`python`、`fortran` 和 `typescript`；输出目标只接受 `javascript` 和 `cpp`。`cpp` 是目标名称，当前生成标准为 C++17。

完整的已支持语法、语义和限制见[语言支持矩阵](docs/LANGUAGE_SUPPORT.md)。Matlab → JavaScript 的成熟度分析、完成定义和专项清单见[产品计划](docs/MATLAB_TO_JAVASCRIPT.md)；跨语言工作见[项目路线图](TODO.md)。

## 环境要求

- CMake 3.20 或更高版本
- 支持 C++17 的编译器
- Node.js：运行生成的 JavaScript 或完整差分测试时需要
- Python 和 gfortran：运行相应源语言差分测试时需要

所有构建产物必须放在仓库根目录的 `build/` 下。

## 构建

开发构建：

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Release 构建：

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

不使用 preset 时：

```sh
cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure
```

## 命令行快速开始

将 Python 转换为 JavaScript 并运行：

```sh
build/release/mpfc --target javascript examples/python/basic.py -o build/basic.mjs
node build/basic.mjs
```

将 Python 转换为 C++、编译并运行：

```sh
build/release/mpfc --target cpp examples/python/basic.py -o build/basic.cpp
c++ -std=c++17 build/basic.cpp -o build/basic
build/basic
```

其他输入语言示例：

```sh
build/release/mpfc --target javascript examples/matlab/basic.m -o build/matlab-basic.mjs
build/release/mpfc --target javascript examples/matlab/sparse_square_solve.m -o build/matlab-sparse.mjs
build/release/mpfc --target cpp examples/fortran/basic.f90 -o build/fortran-basic.cpp
build/release/mpfc --target javascript examples/typescript/basic.ts -o build/typescript-basic.mjs
```

从标准输入读取 fixed-form Fortran：

```sh
build/release/mpfc --language fortran --fortran-form fixed - < examples/fortran/fixed_form.f
```

生成 JSON 诊断或 source map：

```sh
build/release/mpfc --diagnostics-format json --language python input.py
build/release/mpfc --target javascript --source-map build/output.mjs.map input.py -o build/output.mjs
```

查看全部命令行选项：

```sh
build/release/mpfc --help
```

## 作为 C++ 库使用

安装 MPF：

```sh
cmake --install build/release --prefix build/stage
```

在项目中查找当前精确版本：

```cmake
find_package(mpf 0.7.1 EXACT CONFIG REQUIRED COMPONENTS core cpp)
target_link_libraries(my_application PRIVATE mpf::mpf)
```

最小示例：

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

安装包提供 `core`、`javascript` 和 `cpp` component，以及 `mpf::core`、`mpf::backend-javascript`、`mpf::backend-cpp` 和统一入口 `mpf::mpf`。完整集成示例见 [`examples/embedding`](examples/embedding)；配置时传入 `-DMPF_REQUIRED_VERSION=0.7.1`，以保持精确版本匹配。

MPF 0.x 有意只安装静态库。共享库需要先明确符号导出、allocator/所有权和版本协商契约；设置 `BUILD_SHARED_LIBS` 不会把当前内部 C++ ABI 意外暴露为受支持的动态库接口。

## 构建配置

| CMake 选项 | 默认值 | 用途 |
|---|---:|---|
| `MPF_BUILD_CLI` | `ON` | 构建 `mpfc` 命令行工具 |
| `MPF_BUILD_TESTS` | 顶层项目为 `ON` | 构建测试 |
| `MPF_ENABLE_JAVASCRIPT_BACKEND` | `ON` | 构建 JavaScript 后端 |
| `MPF_ENABLE_CPP_BACKEND` | `ON` | 构建 C++ 后端 |
| `MPF_ENABLE_WERROR` | `OFF` | 将编译器警告视为错误 |
| `MPF_ENABLE_SANITIZERS` | `OFF` | 启用 AddressSanitizer 和 UndefinedBehaviorSanitizer |

例如，只构建 C++ 后端：

```sh
cmake -S . -B build/cpp-only \
  -DMPF_ENABLE_JAVASCRIPT_BACKEND=OFF \
  -DMPF_ENABLE_CPP_BACKEND=ON
cmake --build build/cpp-only --parallel
```

## 文档

- [语言支持矩阵](docs/LANGUAGE_SUPPORT.md)
- [命令行与诊断](docs/DIAGNOSTICS.md)
- [安装、测试和质量检查](docs/TESTING.md)
- [架构说明](docs/ARCHITECTURE.md)
- [新增源语言或输出目标](docs/EXTENDING.md)
- [版本策略](docs/VERSIONING.md)
- [开发路线图](TODO.md)
- [更新日志](CHANGELOG-ZH.md)

## 参与贡献

欢迎通过 issue 或 pull request 参与语言支持、目标后端、测试、性能和文档建设。开始前请阅读[贡献指南](docs/CONTRIBUTING.md)，并确保新增语言行为同时包含成功、边界和拒绝场景。

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```
