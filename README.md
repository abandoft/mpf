# MPF

MPF 是一个通过 CMake 构建的跨平台源码转译器，目标是把 Matlab、Python、Fortran 和 TypeScript 程序转换成可维护的现代 JavaScript 或可移植 C++。工程固定 C17/C++17 工具链基线；当前生产源码与公共 API 使用 C++17，尚未提供稳定 C API。

## 当前状态

最新发布版为 **0.3.4**；后续架构收敛与语言覆盖按 [TODO](TODO.md) 继续推进。`FrontendDescriptor` API v5 管理身份、探测、标准版本范围、能力/resource manifest、parser session、语言 AST verifier 和 AST→HIR lowering；`BackendDescriptor` API v5 管理 configuration/runtime supply-chain manifest、TargetProfile、稠密 MIR legalization、alias/effect 输入、capability、私有 semantic/LIR lowering、artifact verifier、semantic dump 和 printer。源语言 builtin 先解析为稳定 intrinsic ID，再由每个目标的稠密绑定表完成映射或专用 lowering。

生产驱动已经实际执行“语言专属 arena AST → HIR → Analyzer `SemanticTable` → MIR + alias/effect analysis → JavaScript LIR/`cpp` LIR → Emitter”。三种 AST 是编译期互不兼容的 PMR arena artifact；名称/作用域、控制流和 Analyzer 输出分别按 HIR ID 稠密存入 revision-checked `NameTable`、`FlowTable` 与 `SemanticTable`，动态类型/确定赋值状态按 `SymbolId` 访问；MIR 显式保存 type/shape/stride/storage/lifetime、tuple/function/reference 签名、call argument region/transfer、block argument、edge actual、循环和选择 CFG，并验证 ownership、arity、dominance、跨函数 call/return 与 metadata 相容性。alias/effect 不再内嵌 MIR，而由独立、可缓存、可验证的 `AliasEffectTable` 保存 storage root、instruction read/write、函数摘要、call-site 实例化和 writable overlap。两个目标通过 backend descriptor API v5 显式消费该表，各自把 borrow/copy/optional-forward、目标函数 ABI、Program/Function scope/declaration、CSR 临时资源、模块/翻译单元拓扑以及表达式和语句 representation 带入私有 LIR v8，再完成 capability、legalization、确定性 rendering 和 LIR verifier。JavaScript 与 `cpp` 的 `ExpressionPlan` 固化目标 form/precedence/token、比较策略、custom/direct call ABI、逐实参 value/optional-forward/box/copy、参数 value/box/optional 访问、first-result、index/slice metadata 和 reshape shape；`StatementPlan` 固化 statement form、condition truthiness、声明初始化、assignment leaf、selector、range/loop-else、section 写回与返回策略，`cpp` 还保存具体递归容器类型及 widening。Renderer 不再读取 `StatementKind`、`ExpressionKind`、`ValueType`、assignment pattern、源 index/shape、builtin binding 或 call transfer，也不接收 `TranspileOptions`、runtime requirement 或函数图；`ModulePlan`/`TranslationUnitPlan` 固化顶层布局，目标 runtime 模板位于独立 catalog 编译单元，最终 emitter 只序列化带 source origin 的 chunk。公共结果同时提供 source map v3、dependency manifest 和逐阶段编译报告。

0.3.4 的架构收尾已经覆盖 arena AST、Analyzer 全量输出 side table、当前语言子集的 MIR CFG/alias、静态一般 rank 的 RESHAPE/direct-section、纯 emitter、source map、fuzz/resource 与性能发布门禁；0.3.5 开发线进一步完成 Analyzer 直写 semantic side table、结构规范化 revision/remap，独立 name/scope、flow 与 MIR alias/effect side table，以及 call argument borrow/copy/optional-forward/lifetime contract。完整官方语言 grammar、动态 rank/广播、精确 N 维 selector region overlap 和稳定插件 ABI 仍明确保持未完成。精确边界见 [TODO](TODO.md)。版本递增规则见 [版本策略](docs/VERSIONING.md)。

- `cpp` 是唯一 C++ 目标身份，当前输出标准为 C++17；代码中不使用 `cpp17` 一类标识符。
- 两个后端彼此独立，生成 C++ 不需要先生成 JavaScript。
- 当前 0.3.5 开发线为 156 项内部测试、48 个差分 case 和 59 项 CTest；另有持续 fuzz smoke、可选 Clang/libFuzzer 与绑定版本号的编译性能 JSON 发布门禁；生产代码行覆盖率实测 89.01%（17502/19663），高于 85% 硬门槛。0.3.4 的封版数据保留在 changelog。
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
find_package(mpf 0.3.4 CONFIG REQUIRED COMPONENTS core cpp)
target_link_libraries(my_application PRIVATE mpf::mpf)
```

导出目标包括 `mpf::core`、按配置存在的 `mpf::backend-javascript` / `mpf::backend-cpp`、共享的 `mpf::backend-common`，以及统一 facade `mpf::mpf`。运行时可用 `mpf::registered_source_languages()`、`mpf::registered_target_languages()`、`mpf::frontend_available(...)`、`mpf::backend_available(...)` 及名称查询 API 检查 catalog 和当前构建能力；请求缺失后端会返回稳定诊断 `MPF0003`。

`examples/embedding/` 展示了安装包的 `find_package(mpf CONFIG REQUIRED)` 集成方式。

## 验证体系

CTest 同时运行公共 API/语义测试、`mpfc` CLI/JSON/source-map 契约、48 个 declarative differential case、生成 C++17 的真实严格编译、fuzz corpus mutation、性能基线和后端隔离安装测试。差分 runner 在同一 case 内直接比较所有可用路径；CI 固定 CPython 3.14、Node.js 24，并以 gfortran 当前可用的严格 `-std=f2018` 模式执行 Fortran oracle（可通过 CMake cache 切换到未来工具链支持的 `f2023`）。GitHub Actions 定义已按快速反馈、跨平台/差分、质量、Sanitizer、覆盖率、性能、安全和发布拆分失败域，目前通过非标准目录名临时停用；详见 [停用的 workflow 职责矩阵](.github/workflows-disabled/README.md)、[测试与差分执行](docs/TESTING.md) 和 [诊断与 CLI 契约](docs/DIAGNOSTICS.md)。

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
