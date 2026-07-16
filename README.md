# MPF

MPF 是一个通过 CMake 构建的跨平台源码转译器，目标是把 Matlab、Python、Fortran 和 TypeScript 程序转换成可维护的现代 JavaScript 或可移植 C++。工程固定 C17/C++17 工具链基线；当前生产源码与公共 API 使用 C++17，尚未提供稳定 C API。

## 当前状态

最新发布版为 **0.33.0**；当前工作树正在开发 0.34 的多层编译器架构。`FrontendDescriptor` API v4 管理身份、探测、标准版本范围/schema manifest、parser、语言 AST verifier 和 AST→HIR lowering；`BackendDescriptor` API v3 管理 TargetProfile、稠密 MIR legalization、capability、私有 semantic/LIR lowering、artifact verifier 和 printer。源语言 builtin 先解析为稳定 intrinsic ID，再由每个目标的稠密绑定表完成映射或专用 lowering。

生产驱动已经实际执行“语言专属 arena AST → HIR → Analyzer `SemanticTable` → MIR → JavaScript LIR/`cpp` LIR → Emitter”。三种 AST 是编译期互不兼容的 PMR arena artifact；Analyzer 的全部外部结果按 HIR ID 稠密存储并检查 revision；MIR 显式保存 type/shape/stride/storage/lifetime/alias、block argument、edge actual、循环和选择 CFG，并验证 ownership、arity、dominance 与 metadata 相容性。两个目标各自完成 capability、legalization、representation/ABI lowering、确定性 rendering 和 LIR verifier，最终 emitter 只序列化带 source origin 的 chunk。公共结果同时提供 source map v3、dependency manifest 和逐阶段编译报告。

0.34 的本轮架构收尾已经覆盖 arena AST、Analyzer 全量输出 side table、当前语言子集的 MIR CFG/alias、静态一般 rank 的 RESHAPE/direct-section、纯 emitter、source map、fuzz/resource 与性能发布门禁；仍未把完整官方语言 grammar、Analyzer 内部直接 side-table 计算、动态 rank/广播/精确 N 维 overlap 或稳定插件 ABI 误报为完成。精确边界见 [TODO](TODO.md)。

- `cpp` 是唯一 C++ 目标身份，当前输出标准为 C++17；代码中不使用 `cpp17` 一类标识符。
- 两个后端彼此独立，生成 C++ 不需要先生成 JavaScript。
- 当前工作树为 143 项内部测试、48 个差分 case 和 58 项 CTest；另有持续 fuzz smoke、可选 Clang/libFuzzer 与编译性能 JSON 发布门禁；本轮实测生产代码行覆盖率为 88.34%（13468/15245），高于 85% 硬门槛。
- 项目仍是经过验证的语言子集，不能宣称完整兼容 Matlab 2024、Python 3.14 或 Fortran 2023。
- TypeScript 6 已进入产品路线图，但当前尚无可声明支持的 TypeScript 前端子集。

准确覆盖范围见 [语言支持矩阵](docs/LANGUAGE_SUPPORT.md)，当前与后续工作边界见 [持续建设路线图](TODO.md)，前后端重构 contract 见 [商业级编译器管线方案](docs/COMPILER_PIPELINE.md)。

## 构建

所有生成文件必须位于仓库根目录的 `build/` 中，CMake 会主动拒绝其他顶层构建目录。

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

不使用 preset 时：

```sh
cmake -S . -B build/dev -DMPF_ENABLE_WERROR=ON
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure
```

要求 CMake 3.20+ 和支持 C++17 的编译器。CI 覆盖 GCC、Clang、AppleClang 和 MSVC。

后端可以独立选择：

```sh
# 只构建 C++17 后端，不编译任何 JavaScript backend 源文件
cmake -S . -B build/cpp-only \
  -DMPF_ENABLE_JAVASCRIPT_BACKEND=OFF \
  -DMPF_ENABLE_CPP_BACKEND=ON

# 只构建 JavaScript 后端
cmake -S . -B build/javascript-only \
  -DMPF_ENABLE_JAVASCRIPT_BACKEND=ON \
  -DMPF_ENABLE_CPP_BACKEND=OFF
```

## 使用

```sh
build/dev/mpfc examples/python/basic.py
build/dev/mpfc --target javascript --language matlab --module esm examples/matlab/basic.m -o build/basic.mjs
build/dev/mpfc --target cpp examples/python/basic.py -o build/basic.cpp
build/dev/mpfc --language fortran - < examples/fortran/basic.f90
build/dev/mpfc --fortran-form fixed --language fortran - < examples/fortran/fixed_form.f
build/dev/mpfc --diagnostics-format json --language python examples/python/basic.py
build/dev/mpfc --source-map build/basic.cpp.map --target cpp examples/python/basic.py -o build/basic.cpp
```

库 API：

```cpp
#include <mpf/transpiler.hpp>

mpf::TranspileOptions options;
options.language = mpf::SourceLanguage::python;
options.target = mpf::TargetLanguage::cpp;
const auto result = mpf::Transpiler{}.transpile("print(40 + 2)\n", options);
if (result.success()) {
  // result.code, result.source_map, result.dependencies, result.report
}
```

安装后可通过 CMake 包 `mpf::mpf` 使用，并要求需要的组件：

```sh
cmake --install build/release --prefix build/stage
```

```cmake
find_package(mpf 0.33 CONFIG REQUIRED COMPONENTS core cpp)
target_link_libraries(my_application PRIVATE mpf::mpf)
```

导出目标包括 `mpf::core`、按配置存在的 `mpf::backend-javascript` / `mpf::backend-cpp`、共享的 `mpf::backend-common`，以及统一 facade `mpf::mpf`。运行时可用 `mpf::registered_source_languages()`、`mpf::registered_target_languages()`、`mpf::frontend_available(...)`、`mpf::backend_available(...)` 及名称查询 API 检查 catalog 和当前构建能力；请求缺失后端会返回稳定诊断 `MPF0003`。

`examples/embedding/` 展示了安装包的 `find_package(mpf CONFIG REQUIRED)` 集成方式。

## 验证体系

CTest 同时运行公共 API/语义测试、`mpfc` CLI/JSON/source-map 契约、48 个 declarative differential case、生成 C++17 的真实严格编译、fuzz corpus mutation、性能基线和后端隔离安装测试。差分 runner 在同一 case 内直接比较所有可用路径；CI 固定 CPython 3.14、Node.js 24，并在 Linux 使用 gfortran `-std=f2023`。sanitizer preset 在整个编译管线启用 ASan/UBSan；独立 quality/coverage preset 执行格式、静态分析和 85% 生产代码行覆盖率门禁，GitHub workflow 另运行 CodeQL、依赖审查和性能报告归档。详见 [测试与差分执行](docs/TESTING.md) 和 [诊断与 CLI 契约](docs/DIAGNOSTICS.md)。

## 工程结构

```text
include/mpf/          公共 API
src/core/             编译流程和公共基础设施
src/source/           源文件、UTF-8 位置和行映射
src/lexer/            公共 token 与扫描基础设施
src/lexers/           Matlab/Python/Fortran 独立词法规则
src/compiler/         parser scratch、intrinsic/binding 与函数图公共机制
src/semantic/         与目标无关的名称绑定、确定赋值和基础类型推导
src/frontends/        Matlab/Python/Fortran 独立语法前端、arena AST 和 AST→HIR visitor
src/backends/         后端 descriptor、capability/legalization、目标 LIR/renderer 与纯 emitter
src/cli/              mpfc 命令行程序
tests/unit/           公共契约单元测试
tests/integration/    语言端到端测试
tests/generated/      生成 C++17 的真实编译执行测试
tests/fuzz/           smoke corpus、deterministic mutation 与 libFuzzer harness
tests/performance/    编译延迟/吞吐/内存/产物/并发发布门禁
docs/                 架构与兼容性文档
examples/             可执行输入样例
```

演进计划在 [TODO.md](TODO.md)，当前设计边界在 [架构文档](docs/ARCHITECTURE.md)，五层目标管线在 [商业级编译器管线方案](docs/COMPILER_PIPELINE.md)，新增语言和目标的接入契约见 [扩展指南](docs/EXTENDING.md)。
