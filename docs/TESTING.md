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

当前开发分支先以 Semantic v23、MIR v29 和双目标 LIR v36 固化独立 `SparseLogicalPlan`：`~`/`&`/`|` 的 static compatible-size shape、operand/result storage 与 preserve-sparse/materialize-dense policy 进入逐层及目标 ABI 验证；随后以 Semantic v24、MIR v30 和双目标 LIR v37 为 `ReductionPlan` 增加 storage policy，并交付 static real/logical rank-2 CSC `all`/`any`；再以 Semantic v25、MIR v31 和双目标 LIR v38 增加独立 `SparseArithmeticPlan`，交付 static compatible-size sparse `+`/`-`、sparse-sparse CSC 保持与 mixed dense materialization；再以 Semantic v26、MIR v32 和双目标 LIR v39 为 `MatrixOperationPlan` 增加 sparse-power storage 与 nonnegative-safe-integer exponent policy，交付 static real/logical square CSC 的 repeated-squaring 方阵幂、zero-power identity 与 logical-to-double promotion；0.7.0 以 Semantic v27、MIR v33 和双目标 LIR v40 增加 finite-complex sparse value domain、复数 CSC storage lifecycle 和按需 complex-sparse 组合 runtime fragment；0.7.1 再以 Semantic v28、MIR v34 和双目标 LIR v41 为 `SparseArithmeticPlan` 增加 finite-real/finite-complex value-domain ABI，交付 complex sparse compatible-size `+`/`-`；0.7.2 以双目标 LIR v42 为 sparse matrix-product 增加 real/complex numeric-domain ABI，交付 complex CSC×CSC、CSC×dense 与 dense×CSC，并把 product kernel 拆为独立按需 fragment。当前差分、生成 runtime 拒绝、性能、fuzz、目标 LIR/dump、按需 runtime 和纯 Emitter 架构检查共同覆盖这些边界。MemorySSA/region-aware DCE/store forwarding 尚未启用，继续按 [TODO](../TODO.md) 推进。

0.7.3 在不改变 Semantic v28/MIR v34 的前提下把双目标 LIR 提升到 v43：Matlab 裸 `return` 的函数结果 `SymbolId`、函数/脚本 return form，以及 JavaScript module-control prelude/body/declaration 拓扑均进入 representation verifier 与 deterministic dump；`disp`/`display` 单字符向量命令形式同时进入 lexer/parser、双目标差分、source map、fuzz 和独立性能场景。通用 command syntax、`try/catch` 与 `arguments` 仍保持失败关闭。

0.7.4 以 Semantic v30、MIR v36 和双目标 LIR v45 固化 Matlab command 的 `ImplicitResultPolicy`、`ans` `SymbolId`、value/discard、`previous_assigned` 和目标 statement form。独立 scanner 的多参数、单引号分组/双写引号、双引号保留、运算符空格消歧，以及 local/builtin 调用、多输出首值、无输出保持、分支确定赋值和 C++ 类型变化失败关闭进入单元/集成、逐层损坏、双目标差分、source map、fuzz 与独立性能预算。裸无参数/限定名/外部 command、`try/catch` 与 `arguments` 继续保持失败关闭。

0.7.5 以 Matlab AST v4、HIR v3、Semantic v31、MIR v37 和双目标 LIR v46 交付结构化异常纵切面：`try`/单一 `catch [exception]`、无 binding handler、正常跳过、运行时失败、嵌套最内层优先、`error`、`rethrow`、exception `identifier`/`message` 和确定赋值均进入 Analyzer。MIR `ExceptionRegion`、protected-block exceptional edge、`catch_exception` storage capture、alias/effect、CFG memory dependence、优化保留/重映射和损坏区域拒绝进入单元测试；JavaScript/C++ 独立 runtime、双目标执行、handler source map、负向语法/参数诊断、差分、fuzz seed 与独立性能预算进入发布门禁。完整 `MException` 对象模型与 `arguments` block 仍按当前边界失败关闭。

0.7.6 以 Semantic v32、MIR v38 和双目标 LIR v47 交付 Matlab indexed-replacement
conformability 第一纵切面：逐层验证 scalar expansion、linear element count、nonsingleton shape、
static/runtime shape source 以及 selection/value shape；未知形状 local-function 参数使用 runtime
dispatch，full-colon selector 必须保持 shape。门禁同时执行静态 row/column singleton 等价、单元素
数组扩展、matrix-shaped numeric selector 的线性 numel、动态线性/多维成功路径、两类动态不相容失败
路径、捕获失败后的目标值不变、双目标 source map、LIR 损坏拒绝、差分 example、fuzz seed 和独立
编译性能场景。一般非 full-colon 动态 assignment 与统一 NDArray owner/view ABI 仍按 P0-B 后续任务
推进。

0.7.7 在此基础上以 Semantic v33、MIR v39 和双目标 LIR v48 完成 P0-B 第二纵切面：
`runtime` selector 只用于类别未知的 Matlab 索引赋值目标，`overwrite_or_grow` 在运行时区分覆盖与
扩容。双目标差分覆盖动态 scalar/numeric/logical/range selector、线性与多维覆盖/间隙扩容、
动态列删除和三维页扩容；生成失败测试验证不相容 RHS 与损坏 mutation plan 均在提交前拒绝，
JavaScript copied-root 和 C++ staged-value 路径保持原目标值与 shape。普通动态标量读取的既有
selector contract 另有回归测试，防止赋值功能污染读取语义。

0.7.8 以 Semantic v34、MIR v40 和双目标 LIR v49 完成 Matlab exception object 与裸无参 local
command 两个纵切面。异常 operation/message-form/stack-policy 的逐层损坏测试、JavaScript/C++17
独立运行时执行、formatted error、cause/report、throw policy、source map、三类生成失败、差分、
fuzz seed、按需 runtime 裁剪和独立性能场景共同验证该合同；bare command 测试同时固定零输入
local function 成功路径，以及赋值和参数遮蔽时仍按变量表达式解析的保守边界。

0.7.9 已发布纵切面以 Matlab AST v5、Semantic v35、MIR v41 和双目标 LIR v51 固化
`arguments` declaration、formal validated rank 与逐 call class/size boundary。parser/version gate、
Analyzer side table、MIR/LIR 损坏事实、ordered default、input/output validation、N 维 scalar
expansion、row/column reshape、23 个无参数 validator、source map、JavaScript/C++ 差分、生成 C++
编译、runtime rejection、fuzz seed 和独立性能 workload 共同覆盖当前纵切面。name-value、
Repeating、parameterized/custom validator 与动态 rank/class ABI 继续使用负向测试保持失败关闭。

后续开发分支的关系 validator 使用 Matlab AST v9、Semantic v38、MIR v45 与双目标 LIR v54。
定向测试覆盖四种比较、literal/前序 input/output reference、optional access、source map，以及
literal/ordinal/formal shape 和目标 opcode/token/symbol/access form 的独立损坏拒绝。双目标
执行覆盖 strict/非 strict 边界、complex-zero-imag storage、NaN threshold 和首错顺序；另以
leading-zero decimal、超宽 integer、binary64 rounding、subnormal、下溢、empty、logical 与
infinite bound corpus 固定跨目标行为。数值 normalization 同时验证非默认 locale 和极长 exponent；
fuzz seed 与既有 argument-validation 性能 workload 同步扩展。当前增量进一步覆盖标准 unary
显式调用与 scalar-bound `mustBeInRange` 的版本、arity、四种边界、双 flag normalization、输入/
输出、optional real-component access、source map、MIR/LIR/生成 ABI 损坏、NaN 和首错顺序。
新增 representation corpus 同时固定 JS scalar/default-array ABI 和两端 scalar/empty `length`/
`numel`；旧 empty/sparse-zero/sparse-indexing 的 oracle 按官方空数组 length=0 规则校正，不能
以两个目标一起输出错误作为通过理由。不宣称其余参数化/custom validator
或 name-value/Repeating 已被完成。

Analyzer 生命周期回归测试主动收紧 expression side-table capacity，再通过 Matlab 默认参数的
嵌套 call、scalar/multi-axis selector、slice bound、indexed mutation、Python tuple/default
cloning 和 Fortran optional actual 触发真实扩容；ASan/UBSan 与 fuzz 检查其引用和容器计划
不会跨递归 normalization 失效。

## 当前开发分支基线

函数依赖回归覆盖 Matlab default 的 forward/transitive local call、Python definition scope 与
Matlab formal scope、相同 spelling 不同 `SymbolId`、variable-callee 非依赖、50,000 节点链与
SCC、重复分析以及 256 张有向图的独立 reachability oracle。名称 verifier 负向测试覆盖跨函数
rebinding、绕过最近 formal shadowing、builtin identity 污染、foreign symbol/node 与循环 scope
parent。C++ resource verifier 从 LIR 独立重建 graph，拒绝损坏 definition order 后才索引 ABI。
`argument_default_functions.m` 在两端执行 defaults 的按需调用和副作用顺序，并另进入严格生成
C++ 编译与 fuzz；`matlab-default-functions` 性能场景编译 128 个前向 default-call 函数。

validator source-call 回归逐项覆盖 28 个标准候选的上下文 builtin identity、bare/explicit 的
callee/formal/threshold/quoted-flag AST 所有权、输入/输出 declaration 顺序，以及 missing/reordered/
foreign/mismatched call、最近 scope 遮蔽、普通 intrinsic 泄漏、semantic marker/source ID、reindex、
MIR 独立 binding inventory 和双目标私有计划损坏。local function/formal/result/assignment 遮蔽
在两端都必须给出 `MPF2062`、不产生目标源码，也不能混入内部 `MPF0005`。两个新 fuzz seed
分别固定 source-call ownership 和同名 validator 遮蔽。尚未以原生 Matlab 执行 custom validator；
当前合同是阻止标准 spelling 误译，真实验证调用 CFG 尚未完成。

命名 validator grammar 回归另覆盖未知 callee、bare/explicit、零实参与任意已支持的嵌套参数
表达式、空实参/分隔符恢复，以及同名 local callee 在旧源版本或不同 arity 下不受标准 gate
误拒绝。标准 availability/arity/threshold ABI 在 binding 后检查；源版本和 source operand 改动
不能绕过 semantic verifier。错误 source call 与合法标准 plan 的间隙按旧强 ID reindex，四语言
source version 从 arena artifact 经 HIR profile 保留到 MIR，MIR 还从自己的版本独立复核标准
availability。catalog 的全部 28 项逆映射、未知
与大小写 spelling，以及无分配静态索引也进入回归。grammar 已解析不代表目标可执行。

生成代码的 compile-only、runtime rejection、plan corruption 与 differential 子构建共享
`generated_toolchain.cmake`，继承主构建的 compiler/generator/platform/toolset 与显式 macOS
deployment target。后者同时进入 CMake cache 和 compiler-identification 子进程环境；contract
测试验证带空格的 compiler/generator、完整参数与空参数不污染既有环境。GCC macOS 回归不依赖
测试调用方额外导出 deployment 环境变量。

| 指标 | 数量/结果 |
|---|---:|
| C++ 单元与集成测试 | 376 项，零失败 |
| CTest | 当前 dev preset 为 201 项普通测试；包含 121 项 differential、1 项 C++ 单元/集成、63 项生成 runtime 拒绝、6 项生成 C++ 编译，以及 fuzz、架构、发布脚本、CLI、后端隔离和安装消费测试；Release 流程另运行不计入普通测试数的独立性能发布目标 |
| Differential corpus | Python 22、Fortran 19、Matlab 76、TypeScript 4，共 121 个 case |
| 工具完整环境执行路径 | 287 条程序路径，另有每 case 一条 oracle |
| 生产代码行覆盖率 | 硬门槛 85%；当前结果以 `coverage-report` workflow artifact 为准 |

## Differential corpus

权威清单位于 [`tests/differential/corpus.cmake`](../tests/differential/corpus.cmake)。当前包含：

- 22 个 Python case：CPython 3.14、Node.js、生成 C++17 与 oracle 四路比较；
- 19 个 Fortran case：gfortran 严格 `-std=f2018` reference mode、Node.js、生成 C++17 与 oracle 四路比较；`MPF_FORTRAN_REFERENCE_STANDARD` 可在工具链支持后切换到 `f2023`；
- 76 个 Matlab case：Node.js、生成 C++17 与 oracle 三路比较；
- 4 个 TypeScript case：Node.js 24 直接执行可擦除类型的 source、生成 JavaScript、生成 C++17 与声明式 oracle 四路比较；覆盖 basic、typed array、lexical block 和 canonical `for`，完整 type-check 仍待接入与 manifest 匹配的 `tsc`。

在 Node.js、CPython 和 gfortran 均可用的工具完整环境中，这 121 个 case 共执行 287 条程序输出路径：121 条生成 JavaScript/Node.js、121 条生成 C++17、22 条 CPython、19 条 gfortran 和 4 条 Node.js source TypeScript 路径；此外每个 case 都有一条声明式 oracle 基线。Matlab `arguments.m` 固定 input/output、class/validator、ordered default、logical/char/empty validator 语义与 R2024b variable-name 成功路径，`argument_conversion.m` 固定 N 维 scalar expansion、column-to-row reshape，以及前序参数完成 logical conversion 后才求值的 default。其余 matrix/sparse/control/exception/dynamic-assignment corpus 继续固定各自已记录合同；所有 case 均执行两个目标 runtime。63 项 runtime-rejection 测试另覆盖 complex-storage realness、非法/超长变量名和既有 shape/broadcast/division/mutation/sparse ABI 污染边界。

`shape_mutation.m` 额外固定 dense direct alias 与 local-function 参数在 growth/write 后仍保持 Matlab value semantics；`complex_sparse_storage.m` 同时固定 sparse copy 在 assignment/growth/zero erase 后通过 immutable root replacement 隔离旧 alias。

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

`mpf.performance.release-gate` 运行两个目标的四十七类编译场景和八路并发 session，重复编译还会逐字节比较代码与 source map。场景覆盖 small、吞吐、深 CFG、大 shape、函数图、TypeScript 吞吐、128 个同根交错 section 调用的 storage-region 分析、branch/loop/index-write memory-dependence fixed point，以及 Matlab return/command、数组、N 维 tensor、logical kernel、logical reduction kernel、矩阵 solve/power、rank-aware/秩亏 solve、condition-aware、diagonal/upper/lower/dense 与 pivoted-tridiagonal/Cholesky/对称不定回退结构感知方阵 solve、动态 `end`、runtime-shape broadcast、shape mutation、dynamic section assignment、empty-array、complex scalar/array、complex square matrix、complex rectangular CPQR、sparse CSC square-solve、sparse matrix-product、sparse scalar-product、sparse element-wise product、sparse arithmetic、sparse square-power、sparse-index、sparse-assignment、sparse-reshape、logical-sparse storage、complex sparse storage lifecycle、sparse-logical operator、argument-validation、前向 default-call 函数链及 exception-object kernel。sparse-product workload 同时覆盖三种 storage 组合；sparse-elementwise workload 覆盖五种 operand form 和双轴广播；sparse-arithmetic workload 覆盖 sparse-sparse `+`/`-`、两类 mixed dense 路径、双向 scalar、row/column/outer expansion 与重复 sparse result；complex-sparse-arithmetic workload 进一步覆盖 complex CSC、mixed real dense、双向 complex scalar、complex row/column expansion 与 value-domain promotion；complex-sparse-multiply workload 覆盖三种 CSC/dense storage 组合、real/logical promotion、零 extent、canonical complex CSC 与重复乘法；sparse-power workload 覆盖 real/logical CSC base、正整数/零次幂、identity、logical promotion 与重复乘法；sparse-logical workload 覆盖 NOT、sparse/dense AND、sparse-sparse/mixed OR、row-column broadcast、scalar 和 storage materialization；sparse-reduction workload 覆盖 numeric/logical CSC 的按列、按行、全维、高于 rank 与零 extent 归约；sparse-reshape workload 覆盖 size vector、推断维度、N 维请求折叠与反复 shape 恢复；sparse-solve workload 同时覆盖 zero/inferred/sized/reserved triplet construction、duplicate accumulation、full/sparse transpose、零维系数、dense/CSC RHS/LHS 与四种 shaped-empty 左右除；logical-sparse workload 覆盖 logical dense/triplet construction、duplicate `any` 及完整 storage lifecycle；dynamic-section-assignment workload 覆盖 runtime scalar/numeric/logical/range selector、线性/多维覆盖与增长、三维页扩容和失败回滚。Matlab 三十九个场景另有独立的最大延迟、最低吞吐和最大产物预算，避免被全局宽阈值掩盖。结果写入 `build/<preset>/performance-report.json`，并由 [`tests/performance/baseline.json`](../tests/performance/baseline.json) 的精确当前版本上限/下限检查延迟、吞吐、峰值 arena 和最大生成大小；performance schema v3 还允许为已命名的重型场景设置独立覆盖值；当前 sparse-index/sparse-assignment/sparse-reshape/sparse-multiply/sparse-scale/sparse-elementwise/sparse-arithmetic/complex-sparse-arithmetic/complex-sparse-multiply/sparse-power/logical-sparse/complex-sparse/sparse-logical/sparse-reduction 覆盖不会放宽其余 Matlab 场景阈值，也不读取旧版本 baseline。性能 workflow 显式运行独立 `mpf-performance` 目标并归档机器可读报告；该非插桩门禁不在普通 CTest、coverage 或 ASan/UBSan 测试集中重复执行，避免重型测试争抢 CPU 后制造伪回归。

质量与覆盖率门禁：

```sh
cmake --preset quality
cmake --build build/quality --target mpf-format-check
cmake --build --preset quality

cmake --preset coverage
cmake --build --preset coverage
```

coverage preset 使用 Clang source-based coverage，将多进程 `.profraw` 合并后排除 `build/`、`tests/`、不贡献 profile 的子构建 isolation case 和已由独立 workflow 拥有的性能阈值，只统计生产源码；报告位于 `build/coverage/coverage/`。当前门槛为 85%，具体实测值只保留在 `coverage-report` workflow artifact，不写入面向用户的 changelog 或 Release 正文。独立 `Security Analysis` workflow 先探测仓库的 GitHub Advanced Security 能力；公共仓库或已授权 GHAS 的私有仓库对 C/C++ 运行 CodeQL `security-extended`，并在 pull request 上拒绝引入 moderate 及以上已知漏洞的依赖变更。未授权私有仓库明确记录 capability notice，并继续依赖始终执行的 clang-tidy/Clang analyzer、`Memory Safety` 和零告警构建门禁。

完整的 workflow 边界、稳定 required check 名称、Release 依赖图、超时和产物策略见
[GitHub Actions 职责矩阵](../.github/workflows/README.md)。主分支、pull request 与 merge queue 使用同一组七类 required workflow；Release 通过 `workflow_call` 在标签 SHA 上复用这些定义，任何门禁失败都会在打包前终止。

新增可执行语言能力时，必须在 manifest 增加 case；若输出中的空白属于语义，使用 `lines` 模式，否则数值/list-directed 输出可使用 `tokens` 模式。只有编译不执行的输入应保留为独立 compile-only gate。
