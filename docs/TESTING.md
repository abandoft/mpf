# 测试与差分执行

MPF 的验证体系分为八层：

1. C++ 单元/集成测试验证公共 API、lexer/parser、AST/HIR/MIR/LIR verifier、pass/analysis、capability/legalization、extension conformance 和 emitter 结构；
2. declarative differential corpus 验证可执行语言语义；
3. javascript-only、cpp-only、core-only 隔离测试验证编译、链接、安装和外部消费边界；
4. Debug、Release、ASan/UBSan 以及 GitHub 多平台矩阵验证构建模式与工具链；
5. clang-format、零告警 clang-tidy、85% 生产代码行覆盖率，以及仓库能力可用时的 CodeQL 和依赖审查构成工程质量门禁。
6. corpus mutation smoke 与可选 Clang/libFuzzer 覆盖四种前端、两个目标、资源耗尽和确定性重放；
7. 小文件延迟、吞吐、深 CFG、大 shape、跨函数图、区域访问、CFG memory dependence、Matlab 数组/tensor/matrix-solve/dynamic-broadcast/complex kernel、峰值 arena、产物大小和并发 session 进入发布性能门禁。
8. Release 在标签提交上重新调用以上 canonical workflow；七类门禁全部成功后，三平台候选才可执行完整功能/差分测试、安装后外部消费、ZIP/许可证/版本/校验和验证、build-provenance attestation、发布及公开资产回读验证。

当前 CFG memory-dependence 单元/负向测试覆盖 revision/count/density/sentinel、强类型 access site、incoming/outgoing adjacency、RAW/flow、WAR/anti、WAW/output、分支多定义合流、自然/不可归约 loop-carried、自环、unknown-memory barrier、同根 disjoint region 消边、full-root hazard 后 frontier kill、确定性 dump、`AnalysisManager` 缓存和损坏 edge 拒绝；`InstructionAttributes`、copy/writeback、跨函数 actual region、alias/conflict 与优化重映射继续回归。生产 API 测试要求编译报告公开 `mir-memory-dependence` stage 和分类计数。Python fuzz seed 覆盖分支/循环/索引写入，第八个 `memory-dependence` 性能场景同时要求最低依赖规模和非零 loop-carried 事实。

0.4.8—0.5.6 依次覆盖 implicit expansion、索引/shape mutation、empty array、rank/condition/structure-aware real solve、logical/reduction 与 portable scalar division。0.5.7—0.5.9 又分别引入 `NumericClass`/`NumericComplexity`、complex square matrix domain 与 rank-revealing rectangular factorization；跨层损坏事实、双目标差分、source map、fuzz、warning 与第 22—24 项性能场景共同验证这些 contract。

0.6.0—0.6.9 按纵切面依次引入 canonical CSC storage、`SparseConstructionPlan`、`SparseIndexPlan`、`SparseMutationPlan`、`SparseReshapePlan`、sparse matrix/scalar product、独立 `SparseElementwisePlan`、静态零 extent shape ABI，以及 `SparseValueDomain`/`SparseDuplicatePolicy`。每个纵切面都要求 Semantic→MIR→双目标 LIR 的逐层 verifier、JavaScript/C++ 独立 runtime、source map、差分、生成代码拒错、fuzz、架构检查和独立或复用的 schema-v3 性能预算；0.6.9 对应 Semantic v22、MIR v28、LIR v35、第 99 项差分 case、第 19 项生成 runtime 拒绝和第 32 项性能场景。

当前开发分支以 Semantic v23、MIR v29 和双目标 LIR v36 固化独立 `SparseLogicalPlan`：`~`/`&`/`|` 的 static compatible-size shape、operand/result storage 与 preserve-sparse/materialize-dense policy 进入逐层及目标 ABI 验证。第 100 项差分 case、第 20 项生成 runtime 拒绝、第 33 项性能场景、专属 fuzz seed、独立目标 LIR 单元测试和纯 Emitter 架构门禁共同覆盖。MemorySSA/region-aware DCE/store forwarding 尚未启用，继续按 [TODO](../TODO.md) 推进。

## 当前开发分支基线

| 指标 | 数量/结果 |
|---|---:|
| C++ 单元与集成测试 | 275 项，零失败 |
| CTest | Debug/Release/RelWithDebInfo 均为 131 项普通测试；包含 100 项 differential、1 项 C++ 单元/集成、20 项生成 runtime 拒绝、1 项 fuzz smoke、1 项编译器分层门禁、1 项发布脚本正/负契约、2 项 CLI、1 项生成 C++ 编译、3 项后端隔离和 1 项安装后示例测试；Release 另启用 1 项性能发布门禁 |
| Differential corpus | Python 22、Fortran 19、Matlab 55、TypeScript 4，共 100 个 case |
| 工具完整环境执行路径 | 245 条程序路径，另有每 case 一条 oracle |
| 生产代码行覆盖率 | 当前开发分支完整门禁为 91.77%（38,499/41,953）；硬门槛 85% |

## Differential corpus

权威清单位于 [`tests/differential/corpus.cmake`](../tests/differential/corpus.cmake)。当前包含：

- 22 个 Python case：CPython 3.14、Node.js、生成 C++17 与 oracle 四路比较；
- 19 个 Fortran case：gfortran 严格 `-std=f2018` reference mode、Node.js、生成 C++17 与 oracle 四路比较；`MPF_FORTRAN_REFERENCE_STANDARD` 可在工具链支持后切换到 `f2023`；
- 55 个 Matlab case：Node.js、生成 C++17 与 oracle 三路比较；
- 4 个 TypeScript case：Node.js 24 直接执行可擦除类型的 source、生成 JavaScript、生成 C++17 与声明式 oracle 四路比较；覆盖 basic、typed array、lexical block 和 canonical `for`，完整 type-check 仍待接入与 manifest 匹配的 `tsc`。

在 Node.js、CPython 和 gfortran 均可用的工具完整环境中，这 100 个 case 共执行 245 条程序输出路径：100 条生成 JavaScript/Node.js、100 条生成 C++17、22 条 CPython、19 条 gfortran 和 4 条 Node.js source TypeScript 路径；此外每个 case 都有一条声明式 oracle 基线。runner 不仅分别检查 oracle，还直接比较可用执行路径。Matlab corpus 覆盖 `switch`、real matrix/solver/logical/reduction/index/shape/empty 既有语义；`sparse_construction_transpose.m` 固定五类 R2024 constructor 在当前静态实数 contract 内的 zero/empty、shape inference、scalar expansion、duplicate cancellation、`nzmax`、real transpose 与 canonical CSC；`logical_sparse.m` 固定 logical dense/triplet construction、duplicate `any`、transpose、scalar/linear/submatrix selection、indexed mutation/growth/deletion、reshape、`full`/`issparse`/`nnz` 及 logical class 保持；`sparse_logical_operators.m` 固定 `~`/`&`/`|` 的 sparse/dense/scalar 组合、row/column compatible-size expansion、零 extent、结果 `nnz` 与 CSC/dense storage；`sparse_indexing.m` 固定 scalar、colon、slice、numeric/logical、重复/乱序/空 selector、vector orientation、numeric-matrix shape 与 Cartesian submatrix，并验证 selection 继续保持 CSC；`sparse_assignment.m` 固定 linear/subscript assignment、dense/sparse RHS、scalar expansion、duplicate last-write-wins、zero erase、growth、column deletion 与 self-alias；`sparse_reshape.m` 固定 size vector、首尾 `[]` 推断、N 维请求折叠与 CSC 列主序保持；`sparse_matrix_product.m` 固定 CSC×CSC、CSC×dense、dense×CSC 的数值和 sparse/dense 结果 storage；`sparse_scalar_product.m` 固定 CSC×scalar、scalar×CSC、logical/negative/zero factor、exact-zero elimination 与 CSC storage；`sparse_elementwise_product.m` 固定 sparse/dense/scalar 五种 operand form、row/column compatible-size expansion、exact-zero removal 与 canonical CSC result；`sparse_square_solve.m` 固定 canonical CSC 转换/查询/计数、三对角与一般主元稀疏方阵左除、右除及 storage-preserving result；`sparse_zero_extent.m` 固定零 extent CSC 的 constructor、transpose、reshape、index、mutation、scale、element-wise、matrix-product 与 `0×0` 方阵左右除，并分别检查 dense/CSC shaped-empty 结果。两项 sparse condition case 继续固定精确奇异/近奇异警告正文和次数；`complex_numbers.m` 覆盖 imaginary literal、`i`/`j`、stable complex division、零指数、一元正负、`complex`/`conj`/`real`/`imag`/`abs`、complex compatible-size array、普通/共轭转置、索引写入/reshape，以及 scalar/array local-function 动态 numeric ABI；`complex_matrix_operations.m`、`complex_dense_pivot_solve.m` 和两项 condition-warning case 固定 complex matrix multiply、Hermitian Cholesky、dense pivoted LU、多 RHS、共轭转置右除、positive/negative power 与 warning；`complex_rectangular_solve.m` 和 `complex_rank_deficient_solve.m` 进一步覆盖复数超定/欠定、多 RHS、左右除、pivoted basic solution 与秩亏 warning。所有 case 均执行两个目标 runtime，另有 20 项生成 runtime 测试固定动态非法 shape/broadcast/division/power、sparse-product operand-plan、sparse-scale nonfinite scalar 与 overflow、sparse-elementwise nonfinite/overflow/plan-shape 污染、零 extent sparse 左右除 result-shape plan 污染、logical sparse construction shape-plan 污染，以及 sparse logical operator storage-plan 污染边界。Python optimization case 固定 checked constant folding 在 source、生成 JavaScript 与生成 C++17 间的结果等价；comparisons case 四路覆盖 equality/identity/membership、list/tuple 种类差异、递归布尔/数值相等和混合 comparison chain；两项 Python 生成 runtime 测试分别要求 `/+0.0` 与 `//-0.0` 在 JavaScript/C++17 中非零退出并报告稳定的 division-by-zero 错误。TypeScript 四路覆盖 default/control/export、strict equality、typed array/零基 mutation、block-local 混合类型遮蔽、外层赋值以及 canonical-for 的 break/continue/update/退出值。Fortran disjoint-regions case 四路覆盖交错 stride 与二维同根 writable block，tensor、SELECT CASE、structured-unpacking、argument association 和 optional writeback cases 继续覆盖原契约。

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

`mpf-fuzz-smoke` 在每次 CTest 中读取 `tests/fuzz/corpus/`，对输入执行截断、bit flip、超深括号和非法字节 mutation；每个输入都经过四种 frontend 与两个 target，并重复比较代码、source map 和诊断的确定性。公共 `ResourceLimits` 对 source bytes、token、parser depth、arena、AST/HIR/MIR/LIR 节点、生成代码和 source map 分阶段失败关闭。

Clang 环境可运行覆盖引导 fuzz：

```sh
cmake -S . -B build/fuzz -DMPF_BUILD_FUZZERS=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build/fuzz --target mpf-transpiler-fuzzer
cmake -E copy_directory tests/fuzz/corpus build/fuzz/corpus
build/fuzz/tests/mpf-transpiler-fuzzer build/fuzz/corpus
```

崩溃输入可直接交给 smoke runner 重放，或使用 libFuzzer `-minimize_crash=1` 最小化；具体命令见 [`tests/fuzz/README.md`](../tests/fuzz/README.md)。

## 性能门禁

`mpf.performance.release-gate` 运行两个目标的三十三类编译场景和八路并发 session，重复编译还会逐字节比较代码与 source map。场景覆盖 small、吞吐、深 CFG、大 shape、函数图、TypeScript 吞吐、128 个同根交错 section 调用的 storage-region 分析、branch/loop/index-write memory-dependence fixed point，以及 Matlab 数组、N 维 tensor、logical kernel、logical reduction kernel、矩阵 solve/power、rank-aware/秩亏 solve、condition-aware、diagonal/upper/lower/dense 与 pivoted-tridiagonal/Cholesky/对称不定回退结构感知方阵 solve、动态 `end`、runtime-shape broadcast、shape mutation、empty-array、complex scalar/array、complex square matrix、complex rectangular CPQR、sparse CSC square-solve、sparse matrix-product、sparse scalar-product、sparse element-wise product、sparse-index、sparse-assignment、sparse-reshape、logical-sparse storage 及 sparse-logical operator kernel。sparse-product workload 同时覆盖三种 storage 组合；sparse-elementwise workload 覆盖五种 operand form 和双轴广播；sparse-logical workload 覆盖 NOT、sparse/dense AND、sparse-sparse/mixed OR、row-column broadcast、scalar 和 storage materialization；sparse-reshape workload 覆盖 size vector、推断维度、N 维请求折叠与反复 shape 恢复；sparse-solve workload 同时覆盖 zero/inferred/sized/reserved triplet construction、duplicate accumulation、full/sparse transpose、零维系数、dense/CSC RHS/LHS 与四种 shaped-empty 左右除；logical-sparse workload 覆盖 logical dense/triplet construction、duplicate `any` 及完整 storage lifecycle。Matlab 二十五个场景另有独立的最大延迟、最低吞吐和最大产物预算，避免被全局宽阈值掩盖。结果写入 `build/<preset>/performance-report.json`，并由 [`tests/performance/baseline.json`](../tests/performance/baseline.json) 的精确当前版本上限/下限检查延迟、吞吐、峰值 arena 和最大生成大小；performance schema v3 还允许为已命名的重型场景设置独立覆盖值；当前 sparse-index/sparse-assignment/sparse-reshape/sparse-multiply/sparse-scale/sparse-elementwise/logical-sparse/sparse-logical 覆盖不会放宽其余 Matlab 场景阈值，也不读取旧版本 baseline。性能 workflow 显式运行独立 `mpf-performance` 目标并归档机器可读报告；该非插桩门禁不在普通 CTest、coverage 或 ASan/UBSan 测试集中重复执行，避免重型测试争抢 CPU 后制造伪回归。

质量与覆盖率门禁：

```sh
cmake --preset quality
cmake --build build/quality --target mpf-format-check
cmake --build --preset quality

cmake --preset coverage
cmake --build --preset coverage
```

coverage preset 使用 Clang source-based coverage，将多进程 `.profraw` 合并后排除 `build/`、`tests/`、不贡献 profile 的子构建 isolation case 和已由独立 workflow 拥有的性能阈值，只统计生产源码；报告位于 `build/coverage/coverage/`。当前门槛为 85%；每个正式版本的实测值记录在 changelog，历史数据不覆写。独立 `Security Analysis` workflow 先探测仓库的 GitHub Advanced Security 能力；公共仓库或已授权 GHAS 的私有仓库对 C/C++ 运行 CodeQL `security-extended`，并在 pull request 上拒绝引入 moderate 及以上已知漏洞的依赖变更。未授权私有仓库明确记录 capability notice，并继续依赖始终执行的 clang-tidy/Clang analyzer、`Memory Safety` 和零告警构建门禁。

完整的 workflow 边界、稳定 required check 名称、Release 依赖图、超时和产物策略见
[GitHub Actions 职责矩阵](../.github/workflows/README.md)。主分支、pull request 与 merge queue 使用同一组七类 required workflow；Release 通过 `workflow_call` 在标签 SHA 上复用这些定义，任何门禁失败都会在打包前终止。

新增可执行语言能力时，必须在 manifest 增加 case；若输出中的空白属于语义，使用 `lines` 模式，否则数值/list-directed 输出可使用 `tokens` 模式。只有编译不执行的输入应保留为独立 compile-only gate。
