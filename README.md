# MPF

MPF 是一个通过 CMake 构建的跨平台源码转译器，目标是把 Matlab、Python、Fortran 和 TypeScript 程序转换成可维护的现代 JavaScript 或可移植 C++。工程固定 C17/C++17 工具链基线；当前生产源码与公共 API 使用 C++17，尚未提供稳定 C API。

## 当前状态

当前开发版为 **0.4.6**；项目尚未形成稳定产品，0.x 只提供当前源码树的精确 contract，不兼容旧 MPF API、ABI、CLI、CMake package、descriptor 或 schema。`FrontendDescriptor` API v6 管理 canonical identity、探测、标准版本范围、能力/resource manifest、parser session、语言 AST verifier 和 AST→窄 HIR + semantic seed lowering；`BackendDescriptor` API v6 管理 canonical target identity、configuration/runtime supply-chain manifest、TargetProfile、稠密 MIR legalization、alias/effect 输入、capability、私有 semantic/LIR lowering、artifact verifier、semantic dump 和 printer。源语言 builtin 先解析为当前稳定 intrinsic ID，再由每个目标的稠密绑定表完成映射或专用 lowering。

生产驱动已经实际执行“语言专属 arena AST → 窄 HIR + frontend `SemanticTable` seed → Analyzer → MIR → 共享 MIR 优化 → alias/effect analysis → CFG memory-dependence → JavaScript LIR/`cpp` LIR → Emitter”。四个 statement parser 直接使用 `FrontendAstBuilder<LanguageTag>` 构造编译期互不兼容的 PMR arena artifact，以 `AstNodeId` 连接表达式、控制流和根节点；共享递归 `Program/Statement` scratch 与整树复制旁路已经删除。MIR 同样不再驻留递归 HIR 兼容树，而以 `MirExpressionId`/`MirStatementId` 连接稠密 expression/operation arena；结构节点绑定 resident instruction，非结构语义位于 revision-bound `OperationAttributeTable`，其中每个 `InstructionId` 另有零到多个带 storage/root/region/read-write mode 的 `MemoryAccess`，type/shape/storage 只用驻留强类型 ID 表达。conditional、`and/or`、comparison chain 与 TypeScript canonical `for` 生成显式 CFG、typed edge merge 和 runtime-independent memory operation。AST→HIR 同时建立按 HIR ID 稠密、绑定 revision 的 semantic seed；名称/作用域与控制流分别存入 `NameTable`、`FlowTable`，其中 profile 驱动的 scope graph 为 function/statement/body/alternative 建立可验证边，Analyzer 的动态类型和确定赋值状态按 `SymbolId` 访问。默认 MIR pass 在两个后端分叉前统一执行 shape canonicalization、block-argument copy propagation、精确整数/布尔常量折叠、dead-pure elimination 和保守 CFG cleanup；逐 pass verifier、revision 同步、instruction-attribute 紧凑重映射与 instrumentation 固化变换边界。alias/effect 随后基于优化后的 revision 重算，将直接访问和跨函数 call-site 实例化统一成区域化 memory-access facts，memory-dependence 再建立 region-refined RAW/WAR/WAW 和 loop-carried 边。两个目标通过 backend descriptor API v6 显式消费同一优化结果和分析事实，各自把 symbol identity、lexical `ScopePlan`、borrow/copy/optional-forward、函数 ABI、CSR 临时资源、模块/翻译单元拓扑、source export 以及 expression/statement representation 带入私有 LIR v12，再完成 capability、legalization、确定性 rendering 和 LIR verifier。最终 emitter 只序列化已映射 chunk，公共结果提供 source map v3、dependency manifest、逐 pass 阶段指标和 MIR 优化统计。

当前 `MemoryDependenceTable` 按函数 CFG 做确定性 fixed point，以强类型 `MemoryDependenceId` 和 `MemoryAccessSite` 建立 RAW/flow、WAR/anti、WAW/output 边，使用 storage root 与 N 维 region 消除可证明不相交的依赖，并为未知内存和循环回边显式标记 barrier/loop-carried。0.4.6 同时删除旧包版本范围、历史 consumer 版本请求、旧式包变量、descriptor 名称 alias 和歧义名称解析 API；安装包只接受精确当前版本，公共解析函数以 `std::optional` 明确失败。MemorySSA、region-aware DCE/store forwarding 尚未启用；完整四语言官方 grammar、动态 rank/广播、一般 view/pointer 区域组合、完整 target AST/ownership node 和稳定插件 ABI 也仍未完成。精确边界见 [TODO](TODO.md)，开发快照与版本策略见 [版本策略](docs/VERSIONING.md)。

- `cpp` 是唯一 C++ 目标身份，当前输出标准为 C++17；代码中不使用 `cpp17` 一类标识符。
- 两个后端彼此独立，生成 C++ 不需要先生成 JavaScript。
- 0.4.6 为 180 项内部测试、55 个差分 case 和 66 项 CTest；工具完整环境执行 155 条程序路径，另有持续 fuzz smoke、可选 Clang/libFuzzer 与绑定版本号的八场景编译性能 JSON 门禁；生产代码行覆盖率基线为 89.74%（22249/24793），高于 85% 硬门槛。
- 项目仍是经过验证的语言子集，不能宣称完整兼容 Matlab 2024、Python 3.14 或 Fortran 2023。
- TypeScript frontend manifest 以 6.0 为上限；这里的版本身份不等于完整 TypeScript 6 grammar 兼容声明。

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
build/dev/mpfc --target javascript examples/typescript/basic.ts -o build/typescript-basic.mjs
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
find_package(mpf 0.4.6 EXACT CONFIG REQUIRED COMPONENTS core cpp)
target_link_libraries(my_application PRIVATE mpf::mpf)
```

导出目标包括 `mpf::core`、按配置存在的 `mpf::backend-javascript` / `mpf::backend-cpp`、共享的 `mpf::backend-common`，以及统一 facade `mpf::mpf`。运行时可用 `mpf::registered_source_languages()`、`mpf::registered_target_languages()`、`mpf::frontend_available(...)`、`mpf::backend_available(...)` 检查 catalog 和当前构建能力；`parse_source_language`、`parse_target_language` 与 `parse_fortran_source_form` 只接受大小写不敏感的 canonical 名称并以 `std::optional` 报告失败。请求缺失后端会返回当前诊断 `MPF0003`。

`examples/embedding/` 展示了安装包的 `find_package(mpf CONFIG REQUIRED)` 集成方式。

## 验证体系

CTest 同时运行公共 API/语义测试、`mpfc` CLI/JSON/source-map 契约、55 个 declarative differential case、生成 C++17 的真实严格编译、fuzz corpus mutation、性能基线和后端隔离安装测试。差分 runner 在同一 case 内直接比较所有可用路径；CI 固定 CPython 3.14、Node.js 24，并以 gfortran 当前可用的严格 `-std=f2018` 模式执行 Fortran oracle（可通过 CMake cache 切换到未来工具链支持的 `f2023`）；TypeScript 可擦除语法额外由 Node.js 24 直接执行 source。GitHub Actions 定义已按快速反馈、跨平台/差分、质量、Sanitizer、覆盖率、性能、安全和发布拆分失败域，目前通过非标准目录名临时停用；详见 [停用的 workflow 职责矩阵](.github/workflows-disabled/README.md)、[测试与差分执行](docs/TESTING.md) 和 [诊断与 CLI 契约](docs/DIAGNOSTICS.md)。

## 工程结构

```text
include/mpf/          公共 API
src/core/             编译流程和公共基础设施
src/source/           源文件、UTF-8 位置和行映射
src/lexer/            公共 token 与扫描基础设施
src/lexers/           Matlab/Python/Fortran/TypeScript 独立表达式词法入口
src/compiler/         表达式 parser、跨层语法身份、intrinsic/binding 与函数图公共机制
src/semantic/         与目标无关的名称绑定、确定赋值和基础类型推导
src/frontends/        四种源语言的独立语法前端、arena AST 和 AST→HIR visitor
src/backends/         后端 descriptor、capability/legalization、目标 LIR/renderer 与纯 emitter
src/cli/              mpfc 命令行程序
tests/unit/           公共契约单元测试
tests/integration/    语言端到端测试
tests/generated/      生成 C++17 的真实编译执行测试
tests/fuzz/           smoke corpus、deterministic mutation 与 libFuzzer harness
tests/performance/    编译延迟/吞吐/内存/产物/并发发布门禁
docs/                 架构与工程文档
examples/             可执行输入样例
```

演进计划在 [TODO.md](TODO.md)，当前设计边界在 [架构文档](docs/ARCHITECTURE.md)，五层目标管线在 [商业级编译器管线方案](docs/COMPILER_PIPELINE.md)，新增语言和目标的接入契约见 [扩展指南](docs/EXTENDING.md)。
