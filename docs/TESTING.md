# 测试与差分执行

MPF 的验证体系分为七层：

1. C++ 单元/集成测试验证公共 API、lexer/parser、AST/HIR/MIR/LIR verifier、pass/analysis、capability/legalization、extension conformance 和 emitter 结构；
2. declarative differential corpus 验证可执行语言语义；
3. javascript-only、cpp-only、core-only 隔离测试验证编译、链接、安装和外部消费边界；
4. Debug、Release、ASan/UBSan 以及 GitHub 多平台矩阵验证构建模式与工具链；
5. clang-format、零告警 clang-tidy、85% 生产代码行覆盖率，以及仓库能力可用时的 CodeQL 和依赖审查构成工程质量门禁。
6. corpus mutation smoke 与可选 Clang/libFuzzer 覆盖四种前端、两个目标、资源耗尽和确定性重放；
7. 小文件延迟、吞吐、深 CFG、大 shape、跨函数图、峰值 arena、产物大小和并发 session 进入发布性能门禁。

0.4.3 在既有 stage/include、AST/HIR/MIR/双目标 LIR verifier、共享优化、source map、resource、fuzz 和性能门禁上新增静态 N 维 storage-region 纵切面。单元/负向测试覆盖 rectangular/linearized/unknown、空集、identical、连续/交错 progression、二维笛卡尔积、非法 bounds、`2^63`/最小 step 转换边界、HIR/MIR side-table 不一致和 writable conflict；集成测试覆盖双后端连续、负步长、交错 stride 与二维 block writeback，并固定重叠或动态边界的 `MPF2038` fail-closed。semantic v2、MIR v5 和 alias-effect v2 dump 公开完整 root shape 与 `first:stride:count`；架构门禁要求公共 region contract 并禁止目标 lowering/renderer 重算。新增 gfortran source、Node.js、严格生成 C++ 与 oracle 四路差分、Fortran fuzz seed，以及第七个 `storage-regions` 性能场景。更广四语言官方 grammar、动态 rank/广播和跨一般 pointer/view 的区域证明继续按 [TODO](../TODO.md) 推进。

## 当前开发分支与发布基线

| 指标 | 数量/结果 |
|---|---:|
| C++ 单元与集成测试 | 177 项，零失败 |
| CTest | 66 项，包含 55 项 differential、1 项 fuzz smoke、1 项性能发布门禁、1 项编译器分层门禁、1 项生成 C++ 编译、3 项后端隔离和 1 项安装后示例测试 |
| Differential corpus | Python 22、Fortran 19、Matlab 10、TypeScript 4，共 55 个 case |
| 工具完整环境执行路径 | 155 条程序路径，另有每 case 一条 oracle |
| 生产代码行覆盖率 | 89.49%（21617/24156），门槛 85% |

## Differential corpus

权威清单位于 [`tests/differential/corpus.cmake`](../tests/differential/corpus.cmake)。当前包含：

- 22 个 Python case：CPython 3.14、Node.js、生成 C++17 与 oracle 四路比较；
- 19 个 Fortran case：gfortran 严格 `-std=f2018` reference mode、Node.js、生成 C++17 与 oracle 四路比较；`MPF_FORTRAN_REFERENCE_STANDARD` 可在工具链支持后切换到 `f2023`；
- 10 个 Matlab case：Node.js、生成 C++17 与 oracle 三路比较；
- 4 个 TypeScript case：Node.js 24 直接执行可擦除类型的 source、生成 JavaScript、生成 C++17 与声明式 oracle 四路比较；覆盖 basic、typed array、lexical block 和 canonical `for`，完整 type-check 仍待接入与 manifest 匹配的 `tsc`。

在 Node.js、CPython 和 gfortran 均可用的工具完整环境中，这 55 个 case 共执行 155 条程序输出路径：55 条生成 JavaScript/Node.js、55 条生成 C++17、22 条 CPython、19 条 gfortran 和 4 条 Node.js source TypeScript 路径；此外每个 case 都有一条声明式 oracle 基线。runner 不仅分别检查 oracle，还直接比较可用执行路径。Python optimization case 固定 checked constant folding 在 source、生成 JavaScript 与生成 C++17 间的结果等价；comparisons case 四路覆盖 equality/identity/membership、list/tuple 种类差异、递归布尔/数值相等和混合 comparison chain；TypeScript 四路覆盖 default/control/export、strict equality、typed array/零基 mutation、block-local 混合类型遮蔽、外层赋值以及 canonical-for 的 break/continue/update/退出值。Fortran disjoint-regions case 四路覆盖交错 stride 与二维同根 writable block，tensor、SELECT CASE、structured-unpacking、argument association 和 optional writeback cases 继续覆盖原契约。

每个 case 在 `build/<preset>/differential/<case>/` 保存：

- `generated.mjs`；
- `generated.cpp`；
- 使用顶层同一 compiler/generator 的嵌套严格 C++ 构建；
- `differential-result.txt`，记录工具、归一化模式和各路径结果。

CI 固定 Python 3.14 和 Node.js 24，配置时启用 `MPF_REQUIRE_DIFFERENTIAL_RUNTIME=ON`，并在成功或失败时上传差分结果与生成源码。Linux job 还安装 gfortran。Matlab 源执行必须等待授权 Matlab runner 或明确批准的 Octave 兼容策略，不能用 Octave 结果冒充 Matlab 2024 语义。

## 本地运行

```sh
cmake --preset dev
cmake --build --preset dev
ctest --test-dir build/dev -L differential --output-on-failure
ctest --preset dev
cmake --build build/dev --target mpf-performance
```

## Fuzz 与资源耗尽

`mpf-fuzz-smoke` 在每次 CTest 中读取 `tests/fuzz/corpus/`，对输入执行截断、bit flip、超深括号和非法字节 mutation；每个输入都经过三种 frontend 与两个 target，并重复比较代码、source map 和诊断的确定性。公共 `ResourceLimits` 对 source bytes、token、parser depth、arena、AST/HIR/MIR/LIR 节点、生成代码和 source map 分阶段失败关闭。

Clang 环境可运行覆盖引导 fuzz：

```sh
cmake -S . -B build/fuzz -DMPF_BUILD_FUZZERS=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build/fuzz --target mpf-transpiler-fuzzer
cmake -E copy_directory tests/fuzz/corpus build/fuzz/corpus
build/fuzz/tests/mpf-transpiler-fuzzer build/fuzz/corpus
```

崩溃输入可直接交给 smoke runner 重放，或使用 libFuzzer `-minimize_crash=1` 最小化；具体命令见 [`tests/fuzz/README.md`](../tests/fuzz/README.md)。

## 性能发布门禁

`mpf.performance.release-gate` 运行两个目标的七类编译场景和八路并发 session，重复编译还会逐字节比较代码与 source map。场景覆盖 small、吞吐、深 CFG、大 shape、函数图、TypeScript 吞吐，以及 128 个同根交错 section 调用的 storage-region 分析。结果写入 `build/<preset>/performance-report.json`，并由 [`tests/performance/baseline.json`](../tests/performance/baseline.json) 的版本化上限/下限检查延迟、吞吐、峰值 arena 和最大生成大小。该 Release 性能门禁不在 ASan/UBSan 配置中注册，避免用插桩开销污染发布阈值；独立 `Performance` workflow 显式运行非插桩 `mpf-performance` 并归档机器可读报告。

质量与覆盖率门禁：

```sh
cmake --preset quality
cmake --build build/quality --target mpf-format-check
cmake --build --preset quality

cmake --preset coverage
cmake --build --preset coverage
```

coverage preset 使用 Clang source-based coverage，将多进程 `.profraw` 合并后排除 `build/`、`tests/`、不贡献 profile 的子构建 isolation case 和已由独立 workflow 拥有的性能阈值，只统计生产源码；报告位于 `build/coverage/coverage/`。当前门槛为 85%；每个正式版本的实测值记录在 changelog，历史数据不覆写。独立 `Security` workflow 先探测仓库的 GitHub Advanced Security 能力；公共仓库或已授权 GHAS 的私有仓库对 C/C++ 运行 CodeQL `security-extended`，并在 pull request 上拒绝引入 moderate 及以上已知漏洞的依赖变更。未授权私有仓库明确记录 capability notice，并继续依赖始终执行的 clang-tidy/Clang analyzer、Sanitizer 和零告警构建门禁。

完整的 workflow 边界、required check 名称、超时和产物策略见 [临时停用的 GitHub Actions 职责矩阵](../.github/workflows-disabled/README.md)。恢复自动执行时应将该目录整体改回 `.github/workflows/`。

新增可执行语言能力时，必须在 manifest 增加 case；若输出中的空白属于语义，使用 `lines` 模式，否则数值/list-directed 输出可使用 `tokens` 模式。只有编译不执行的输入应保留为独立 compile-only gate。
