# 测试与差分执行

MPF 的验证体系分为七层：

1. C++ 单元/集成测试验证公共 API、lexer/parser、AST/HIR/MIR/LIR verifier、pass/analysis、capability/legalization、extension conformance 和 emitter 结构；
2. declarative differential corpus 验证可执行语言语义；
3. javascript-only、cpp-only、core-only 隔离测试验证编译、链接、安装和外部消费边界；
4. Debug、Release、ASan/UBSan 以及 GitHub 多平台矩阵验证构建模式与工具链；
5. clang-format、零告警 clang-tidy、85% 生产代码行覆盖率，以及仓库能力可用时的 CodeQL 和依赖审查构成工程质量门禁。
6. corpus mutation smoke 与可选 Clang/libFuzzer 覆盖三种前端、两个目标、资源耗尽和确定性重放；
7. 小文件延迟、吞吐、深 CFG、大 shape、跨函数图、峰值 arena、产物大小和并发 session 进入发布性能门禁。

0.3.4 已覆盖生产 stage/include architecture test、AST/HIR/MIR/双目标 LIR verifier negative、normalized HIR 与双目标 semantic LIR golden、人类可读目标 LIR dump、analysis revision/preservation、source map v3、编译报告、前后端 conformance、安装后 consumer、细粒度 resource exhaustion、fuzz smoke/libFuzzer 和绑定项目版本的性能回归门禁。0.3.5 新增 tuple/function/reference 类型签名、跨函数 call-site/return、Analyzer 直写及独立 flow/name/alias-effect table，borrow/copy/optional-forward/lifetime/writable-overlap contract，以及 LIR v7 函数 ABI/CSR temporary/scope/declaration/module/translation-unit/expression representation inventory 的缺失、乱序与不一致拒绝。新增 representation test 覆盖 JS first-result/reference-box/index selector 与 `cpp` concrete vector/widening/N-D selector；架构门禁禁止 renderer 接收 options、读取 runtime/function graph、共享 expression kind/intrinsic/binding/transfer 或重新扫描布局，并要求 runtime catalog 与 representation planner/verifier 独立编译。内部测试现为 155 项，生产代码行覆盖率实测 88.82%（17096/19248）。更广官方 grammar、精确 N 维 selector region overlap 及宽 IR 收敛继续按 [TODO 0.3.5](../TODO.md) 推进。

## 当前开发分支与发布基线

| 指标 | 数量/结果 |
|---|---:|
| C++ 单元与集成测试 | 155 项，零失败 |
| CTest | 59 项，包含 48 项 differential、1 项 fuzz smoke、1 项性能发布门禁、1 项编译器分层门禁、1 项生成 C++ 编译、3 项后端隔离和 1 项安装后示例测试 |
| Differential corpus | Python 20、Fortran 18、Matlab 10，共 48 个 case |
| 工具完整环境执行路径 | 134 条程序路径，另有每 case 一条 oracle |
| 生产代码行覆盖率 | 88.82%（17096/19248），门槛 85% |

## Differential corpus

权威清单位于 [`tests/differential/corpus.cmake`](../tests/differential/corpus.cmake)。当前包含：

- 20 个 Python case：CPython 3.14、Node.js、生成 C++17 与 oracle 四路比较；
- 18 个 Fortran case：gfortran 严格 `-std=f2018` reference mode、Node.js、生成 C++17 与 oracle 四路比较；`MPF_FORTRAN_REFERENCE_STANDARD` 可在工具链支持后切换到 `f2023`；
- 10 个 Matlab case：Node.js、生成 C++17 与 oracle 三路比较。

在 Node.js、CPython 和 gfortran 均可用的工具完整环境中，这 48 个 case 共执行 134 条程序输出路径：48 条 Node.js、48 条生成 C++17，以及 20 条 CPython 和 18 条 gfortran 源语言路径；此外每个 case 都有一条声明式 oracle 基线。runner 不仅分别检查 oracle，还直接比较可用执行路径。新增 Fortran tensor case 实际执行三维 RESHAPE、任意 rank section 读写和递归 C++ runtime；Python expression-semantics case 四路覆盖 comparison chain 的中间操作数单次求值与短路、条件表达式的惰性分支和右结合，以及 bool/number 和当前 list equality；既有 SELECT CASE、structured-unpacking、argument association 和 optional writeback cases 继续覆盖原契约。

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

`mpf.performance.release-gate` 运行两个目标的五类编译场景和八路并发 session，重复编译还会逐字节比较代码与 source map。结果写入 `build/<preset>/performance-report.json`，并由 [`tests/performance/baseline.json`](../tests/performance/baseline.json) 的版本化上限/下限检查延迟、吞吐、峰值 arena 和最大生成大小。独立 `Performance` workflow 显式运行 `mpf-performance` 并归档机器可读报告。

质量与覆盖率门禁：

```sh
cmake --preset quality
cmake --build build/quality --target mpf-format-check
cmake --build --preset quality

cmake --preset coverage
cmake --build --preset coverage
```

coverage preset 使用 Clang source-based coverage，将多进程 `.profraw` 合并后排除 `build/`、`tests/`、不贡献 profile 的子构建 isolation case 和已由独立 workflow 拥有的性能阈值，只统计生产源码；报告位于 `build/coverage/coverage/`。当前门槛为 85%，0.3.5 开发线实测 88.82%（17096/19248）；0.3.4 封版数据保留在 changelog。独立 `Security` workflow 先探测仓库的 GitHub Advanced Security 能力；公共仓库或已授权 GHAS 的私有仓库对 C/C++ 运行 CodeQL `security-extended`，并在 pull request 上拒绝引入 moderate 及以上已知漏洞的依赖变更。未授权私有仓库明确记录 capability notice，并继续依赖始终执行的 clang-tidy/Clang analyzer、Sanitizer 和零告警构建门禁。

完整的 workflow 边界、required check 名称、超时和产物策略见 [临时停用的 GitHub Actions 职责矩阵](../.github/workflows-disabled/README.md)。恢复自动执行时应将该目录整体改回 `.github/workflows/`。

新增可执行语言能力时，必须在 manifest 增加 case；若输出中的空白属于语义，使用 `lines` 模式，否则数值/list-directed 输出可使用 `tokens` 模式。只有编译不执行的输入应保留为独立 compile-only gate。
