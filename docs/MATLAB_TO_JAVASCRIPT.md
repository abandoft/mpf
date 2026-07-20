# Matlab → JavaScript 产品计划

本文是 `matlab2js` 的专项产品路线图和验收清单。通用编译器边界以
[COMPILER_PIPELINE.md](COMPILER_PIPELINE.md) 为准，已经发布的准确能力以
[LANGUAGE_SUPPORT.md](LANGUAGE_SUPPORT.md) 为准，跨项目优先级仍由根目录
[TODO.md](../TODO.md) 管理。

## 当前结论

当前实现是一个架构完整、可端到端执行的 **Matlab 基础子集**，不是完整或接近完整的
Matlab 2024 转译器。它已经证明语言专属 arena AST、HIR、MIR、JavaScript LIR、纯
renderer、source map 和差分框架能够工作，但还不能安全承载一般生产 Matlab 工程。

| 维度 | 当前状态 | 商用阻断项 |
|---|---|---|
| 源码与语句 | logical statement、注释、续行、脚本、local function、分支、循环和标量 `switch` | 完整 command syntax、`try/catch`、workspace 声明、包/类、产生式级 recovery |
| 表达式 | real/complex 标量与数组算术、imaginary literal、可遮蔽 `i`/`j`、`complex`/`conj`/`real`/`imag`/`abs`、complex 共轭/普通转置；real 关系比较、`~`/`&`/`|` compatible-size N 维逐元素逻辑、scalar `&&`/`||` 与 condition-context `&`/`|` 短路、数组 condition 全元素 truthiness、`all`/`any` 默认/显式/全维逻辑归约、二维 real/complex 矩阵乘法、静态 finite-real rank-2 sparse×sparse/sparse×dense/dense×sparse 矩阵乘法、双向 sparse/scalar 缩放、sparse/dense/scalar compatible-size `.*`、static compatible-size real/logical/complex sparse `+`/`-`、静态 rank-2 sparse logical `~`/`&`/`|` 及非负 safe-integer real/logical CSC 方阵幂、静态稠密实数方阵的 diagonal/upper/lower/pivoted-tridiagonal/symmetric-positive-definite/dense 结构感知求解、稠密 complex 方阵的 Hermitian-positive-definite Cholesky/dense LU 求解、rank-aware real/complex 矩形基本最小二乘求解、静态 real rank-2 CSC 方阵求解（含 `0×0` 系数与 shaped-empty 两侧操作数/结果）及 real/complex safe-integer 方阵幂、R2024 全部 `sparse` 调用形式对静态 real/logical/complex rank-2 值输入（包含零 extent）的构造、普通/共轭转置、scalar/linear/submatrix indexing、indexed assignment/growth/deletion 与列主序 reshape（size vector、维度列表、单个 `[]` 推断及 N 维请求折叠）、`full`/`issparse`/`nnz`、调用、向量/矩阵字面量、静态及运行时 extent 的上下文 `end`、colon/index/section、保序/重复/空 numeric selector、线性及逐维 logical selector | sparse rectangular solve、complex sparse matrix-product/scalar-product/element-wise/power/solve 与动态 sparse shape，complex comparison/logical/reduction，病态方阵特定解选择的精确 Matlab 对齐和非整数矩阵幂 |
| 稀疏归约 | static real/logical rank-2 CSC `all`/`any` 的默认维度、常量 `dim`/`vecdim`、`'all'`、高于 rank 的 no-op 维度与零 extent；非标量结果保持 canonical logical CSC，全维结果返回 full logical scalar；双目标 O(nnz + output-extent) runtime 按需装载且不物化 dense source | 动态或 complex sparse source、动态 `dim`/`vecdim` 与未知 rank 的维度保持型 sparse reduction |
| 数据模型 | `NumericClass` 的 logical/signed-integer/binary64 与独立 real/complex complexity、boolean、字符文本的当前子集、矩形嵌套数组、静态 real/logical/complex rank-2 canonical CSC（包含零 extent）与类型化 `SparseConstructionPlan`/`SparseIndexPlan`/`SparseMutationPlan`/`SparseReshapePlan`/`SparseArithmeticPlan`/`SparseElementwisePlan`/`SparseLogicalPlan`/`ReductionStoragePolicy`/`sparse_csc_multiply`/`sparse_csc_scale`/`sparse_csc_power` storage policy 与 `MatrixExponentPolicy`、带不可枚举 shape descriptor 的 JavaScript 零 extent 数组；JavaScript complex object 与 C++ `std::complex<double>` ABI | single/其余整数 class、一般动态 sparse shape、string、cell、struct、table、datetime、对象 |
| 数组语义 | 1-based、列主序、规范 `0×0` empty、静态零 extent reshape/transpose/broadcast/section/growth、静态 N 维 reshape/section，以及静态 shape 或 local-function runtime rank/extent 的 compatible-size 隐式扩展；complex 数组逐元素/broadcast、索引/写入/reshape/转置；二维 real/complex 矩阵乘法、rank-aware real/complex rectangular solve、real/complex 稠密方阵 solve 与 integer power、广义 selector 读写、vector/matrix/N 维多轴扩容，以及 `all`/`any` 的 N 维/零 extent 归约、符合 null-assignment 规则的单轴删除；当前 JavaScript 输出对 dense name-to-name assignment/local-function 参数使用显式 value-copy plan，对 sparse indexed mutation 使用 immutable root replacement；稀疏子集支持全部 R2024 constructor form、scalar expansion、real/complex 重复项求和/抵消、logical 重复项 `any` 聚合、保持值域的 transpose/index/mutation/reshape/`full`、scalar/linear/submatrix indexing、indexed assignment、重复下标 last-write-wins、zero erase、静态扩容与合法 null deletion，并按 Matlab 左除 RHS/右除 LHS、sparse×sparse/mixed matrix product、双向 sparse/scalar 缩放、非负 safe-integer sparse square power、compatible-size sparse `.*` scalar/dense/sparse、static compatible-size sparse `+`/`-` 及 sparse logical storage 规则保持零 extent 与结果 storage；sparse-sparse `+`/`-`、`~S`、sparse AND 与 sparse-sparse OR 保持 CSC，mixed sparse arithmetic 或 mixed OR 物化 dense | 可跨函数携带不可结构恢复 shape 的统一动态 NDArray/typed-array ABI、其余 sparse 操作与非整数幂、完整动态 bounds，以及一般 NDArray/view 的 copy-on-write、escape 和 alias 契约 |
| 函数 | 文件级 local function、前向调用、单/多输出 | `nargin`/`nargout`、`varargin`/`varargout`、function handle、anonymous/nested closure、workspace |
| JavaScript runtime | 内嵌数组、feature-gated complex object/numeric dispatch、checked non-enumerable shape descriptor、canonical CSC 直接 typed triplet construction/sparse transpose/indexing 与事务式 assignment/deletion、CSC×CSC scatter-accumulator、两类 nonzero-driven mixed matrix-product kernel、sparse arithmetic CSC-column merge/mixed dense materialization、五种 sparse element-wise nonzero-driven kernel，以及 sparse logical NOT/AND/OR 的 CSC 候选扫描与 mixed-OR dense materialization、CSC repeated-squaring sparse power、广义 selector/section、zero-extent reshape/broadcast/transpose、结构感知实数方阵/矩形求解、CSC 三对角/行主元 LU 方阵求解、Hermitian/dense complex 方阵与 CPQR 复数矩形求解、real/complex 矩阵幂和基础 intrinsic runtime | 有版本的 Matlab runtime 包、统一 NDArray ABI、其余 sparse runtime、完整数值/异常兼容层、依赖与许可证审计 |
| 验证 | Node.js、生成 C++、oracle、source map、专项 fuzz seed、跨层 storage 损坏事实拒绝、Matlab 编译性能发布阈值 | 授权 Matlab reference runner；真实项目 corpus；运行时性能、数值精度和内存发布阈值 |

矩阵 `*` 与逐元素 `.*` 保留不同源操作身份，并分别进入目标专属 runtime call plan。0.4.8
进一步交付静态已知 shape 的 Matlab-first-dimension 隐式扩展、数组关系比较、vector/rank-2
转置、逐索引位置 `end` 以及单个线性 mask 的逻辑读写；这些事实均无损通过 HIR、MIR 和目标
LIR。0.4.9 又为静态满秩稠密实数矩阵交付方阵部分主元求解、超定最小二乘、欠定最小
范数、矩形右除以及 safe-integer 正/零/负方阵整数幂，并以强类型 matrix-operation/solve
plan 贯穿各层；索引进一步覆盖保序和重复 numeric selector、empty selector、线性/逐维
logical selector，并在生成 runtime 中验证动态已知的 mask extent。0.5.0 将 `end` 的静态常量快路与
动态 axis/linear extent plan 贯穿 HIR、MIR 和两套目标 LIR；compatible-size 运算另以
static-extents/runtime-operands shape source 区分编译期快路与 local function 参数的运行时 rank/extent，
两套 runtime 独立验证 rectangularity、singleton 兼容性及标量/数组结果。0.5.1 进一步将
overwrite/resize/grow/erase 建模为强类型索引 mutation contract，支持 vector、matrix 和一般 N 维
多轴扩容/单轴删除、列主序线性扩容、运行时标量 selector 与间隙默认值，并由 JavaScript/C++ 两套
独立 runtime 执行。0.5.2 将 `[]` 固定为 `0×0` double，并以 Semantic v8、MIR v13 和目标
LIR v19 的 shaped-empty/shape plan 保持零 extent reshape、transpose、broadcast、section 和 growth；
JavaScript 使用不可枚举 descriptor，C++17 则在静态可知路径消费 input/result shape。随后
`MatrixConditionPolicy` 将矩形 `\`/`/` 固定为 rank-aware CPQR 基本最小二乘解：欠定问题不再错误地产生
最小范数解，数值秩亏返回 pivoted basic solution 并在两目标稳定警告。方阵由显式
`MatrixStructurePolicy::classify_real_square` 选择运行时结构检测：exact-zero diagonal、
upper/lower-triangular、full tridiagonal、exact-symmetric positive-definite 和 dense fallback
按固定优先级分派。三对角路径使用 LAPACK 风格相邻行部分主元紧凑 LU，正定路径只有完整
Cholesky 成功后才启用；对称不定矩阵回退部分主元 dense LU。右除在转置系统上复用同一分类。
各路径以对应正/转置求解构造 1-范数条件估计；`rcond==0` 与 `0<rcond<eps` 分别产生
singular/nearly-singular warning 后继续返回 IEEE `Inf`/`NaN` 或有限病态解。两目标不要求病态
数值逐位一致；容差结构分类仍未实现，R2024 full symmetric-indefinite 使用的
dense LU 行为已由当前 fallback 覆盖。
0.5.7 新增与 shape 正交的 `NumericClass`/`NumericComplexity` side table：trailing `i`/`j`
literal、可遮蔽 imaginary-unit builtin、complex scalar 算术/幂/一元正负、标量
`complex`/`conj`/`real`/`imag`/`abs`、complex 数组 compatible-size 逐元素运算、索引写入、
reshape 以及共轭/普通转置均通过 Semantic v11、MIR v17 和双目标 LIR v23。无类型 local
function 参数保留 unknown complexity，并由目标私有动态 numeric helper 在运行时选择 real 或
complex 路径；JavaScript 使用不可伪造 tag 的 object ABI，`cpp` 使用 `std::complex<double>`，
任一目标的生成均不依赖另一目标产物。0.5.8 又以 Semantic v12、MIR v18 和双目标 LIR v24
加入显式 `MatrixNumericDomain` 与 `classify_complex_square`：二维 complex matrix multiply、
Hermitian-positive-definite Cholesky、按模选主元 dense LU、多 RHS 左除、共轭转置右除、
1-范数条件 warning 和 safe-integer 正/零/负矩阵幂均由两套独立 runtime 执行。0.5.9
以 Semantic v13、MIR v19 和双目标 LIR v25 新增独立 `MatrixFactorizationPolicy`，将 real/complex
矩形 `\`/`/` 统一固定为 rank-revealing CPQR；复数路径使用 Hermitian Householder、多 RHS
共享分解、working-precision rank warning 与共轭转置右除，JavaScript/C++ runtime 仍完全独立。
0.6.0 又以 Semantic v14、MIR v20 和双目标 LIR v26 将 `ArrayStorageFormat`、
`MatrixStoragePolicy` 与 storage-preserving result contract 贯穿各层；静态 real rank-2
`sparse(A)` 使用 canonical CSC，方阵系数优先走 O(nnz) 三对角分解，其他结构走带部分行主元
和稀疏 fill-in 的 LU，正/转置求解共同参与 1-范数条件估计。0.6.1 再以 Semantic v15、
MIR v21 和双目标 LIR v27 增加 `SparseConstructionPlan`，覆盖 R2024 五类 `sparse` 调用形式在
当前非空静态 real rank-2 contract 内的 zero/empty、推断/显式 shape、scalar expansion、重复项
累加/抵消、`nzmax` 与 real sparse 普通/共轭转置；两个目标都直接建立 canonical CSC，且逐层
verifier 会拒绝损坏计划。0.6.2 以 Semantic v16、MIR v22 和双目标 LIR v28 增加
`SparseIndexPlan`，交付一个或两个 selector 的只读 scalar/linear/submatrix indexing；支持 colon、
slice、保序/重复/空 numeric 与 logical selector，scalar 返回 dense value，其余结果保持 CSC。
线性结果遵循 Matlab 的 vector/selector shape 规则，双下标结果为 Cartesian product；`A(:)` 走
O(nnz) 快路，其他路径也不物化 dense source。0.6.3 以 Semantic v17、MIR v23 和双目标 LIR v29 增加 `SparseMutationPlan`；线性/双下标赋值、scalar expansion、dense/sparse RHS、重复下标 last-write-wins、exact-zero entry erase、静态扩容、合法 null deletion 与 self-alias 由双目标独立 canonical-CSC runtime 事务式执行。0.6.4 以 Semantic v18、MIR v24 和双目标 LIR v30 增加 `SparseReshapePlan`；size vector、逗号分隔维度、单个 `[]` 推断与 N 维请求折叠均在分析期固定为输入/请求/二维结果 shape，双目标以 O(nnz + result-columns) 直接重映射 CSC 列主序线性位置。0.6.5 以 Semantic v19、MIR v25 和双目标 LIR v31 增加 `MatrixStoragePolicy::sparse_csc_multiply`；CSC×CSC 使用按结果列 scatter accumulator 直接产生 canonical CSC，CSC×dense 和 dense×CSC 只遍历 CSC 非零项并直接产生 dense 结果。
0.6.6 以 Semantic v20、MIR v26 和双目标 LIR v32 增加
`MatrixStoragePolicy::sparse_csc_scale`；CSC×scalar 与 scalar×CSC 均保持 canonical CSC，非零
finite real/logical factor 只遍历 nnz，零因子以 O(columns) 返回同 shape 空 CSC，underflow exact-zero
不会留在存储中；在 0.6.6 时 sparse logical/逐元素/幂、矩形、complex、零 extent 与动态 source shape 仍以
`MPF2054` 失败关闭；complex comparison/logical/reduction 与非整数
矩阵幂也仍失败关闭。
0.6.7 以 Semantic v21、MIR v27 和双目标 LIR v33 增加独立
`SparseElementwisePlan`，不复用矩阵 `*` 的计划。CSC×scalar、scalar×CSC、CSC×dense、
dense×CSC 与 CSC×CSC 五种 `.*` form 均保存左右/结果 shape、storage 和 compatible-size
broadcast axis；两个目标按非零项驱动或合并 CSC 列完成 singleton row/column expansion，
删除 exact-zero result 并直接返回 canonical CSC；在 0.6.7 时 complex、零 extent 与动态 sparse shape
仍以 `MPF2054` 失败关闭。0.6.8 进一步把全部已支持的静态 real rank-2 sparse
constructor/transpose/index/mutation/reshape/product/scale/element-wise 与 square solve 扩展到零
extent，并将双目标 LIR 提升到 v34。目标 `runtime_shape_arguments` 明确保存 runtime helper 的 shape
实参 ABI，`0×0 \ 0×n` 和 `m×0 / 0×0` 可分别保持 dense/CSC shaped-empty 结果；renderer 只序列化
该目标计划，不再读取 semantic storage/shape policy。complex 与动态 sparse shape 继续以
`MPF2054` 失败关闭。
0.6.9 以 Semantic v22、MIR v28 和双目标 LIR v35 为 `SparseConstructionPlan` 增加
`SparseValueDomain` 与 `SparseDuplicatePolicy`。静态 logical dense/triplet 输入直接产生 typed
canonical CSC，重复 logical triplet 采用 Matlab R2024 的 `any` 规则；转置、索引、mutation、
reshape 与 `full` 全程保持 logical class。JavaScript CSC 携带不可省略的 `valueDomain`，`cpp`
使用 `sparse_matrix<bool>`；两个 representation planner 独立选择 runtime integer ABI/helper，
renderer 仍只序列化已验证目标计划。logical sparse 的算术、逐元素逻辑、归约、幂和求解没有
随 storage 纵切面扩大，继续失败关闭。
当前开发分支进一步以 Semantic v23、MIR v29 和双目标 LIR v36 增加独立
`SparseLogicalPlan`。静态 rank-2 `~`/`&`/`|` 保存 operation、compatible-size shape、逐轴
broadcast、两侧/结果 storage 与 preserve-sparse/materialize-dense policy；`~S`、至少一侧
sparse 的 AND 和 sparse-sparse OR 直接运行 CSC kernel，mixed sparse-dense/scalar OR 明确
物化 dense。两个目标 planner 分别选择 helper 并固化 shape/integer ABI，renderer 仍只序列化；
逐层/目标 LIR/生成代码计划篡改拒绝、零 extent、双目标差分、source map、fuzz、架构和第
33 项性能预算共同验证该边界。此阶段尚未交付的 sparse 归约、算术、幂和求解继续失败关闭。
当前开发分支再以 Semantic v24、MIR v30 和双目标 LIR v37 扩展同一 `ReductionPlan`，加入
`ReductionStoragePolicy`、input storage 与 result storage。静态 real/logical rank-2 CSC 的
`all`/`any` 保持默认/显式/全维 shape 语义：非标量结果直接产生 canonical logical CSC，标量
结果为 full logical。JavaScript/C++ 的独立归约 kernel 只扫描 CSC 非零项及输出维度，且作为依赖
sparse base 的可选 runtime fragment 按需发射；四组 shape 与四个 integer argument 由目标 LIR
完整固化并由 renderer 纯序列化。逐层/LIR/生成计划损坏拒绝、双目标执行差分、source map、fuzz、
架构隔离和第 34 项性能预算共同验证该边界。
当前开发分支随后以 Semantic v25、MIR v31 和双目标 LIR v38 增加独立
`SparseArithmeticPlan`。静态 real/logical rank-2 CSC 的 `+`/`-` 保存 operation、三组
compatible-size shape、逐轴 broadcast、两侧/结果 storage 与 preserve-sparse/materialize-dense
policy。两侧均为 CSC 时，JavaScript/C++ 分别按列归并 entry 并直接产生 canonical double CSC；
mixed sparse-dense/scalar 路径只物化 dense result，不物化 sparse source。两个 representation
planner 独立固定 helper、三组 shape 与五个 integer ABI，renderer 仍只做纯序列化；目标专属
`sparse_arithmetic` runtime fragment 仅按需装载，并依赖基础 `sparse_matrices` fragment。singleton
row/column expansion、logical 数值提升、exact-zero cancellation、零 extent、nonfinite/overflow
拒绝、逐层/LIR/生成 ABI 损坏拒绝、双目标执行差分、source map、fuzz、架构隔离和第 35 项性能预算
共同验证该边界。
当前开发分支再以 Semantic v26、MIR v32 和双目标 LIR v39 为同一 `MatrixOperationPlan` 增加
`MatrixStoragePolicy::sparse_csc_power` 与 `MatrixExponentPolicy::nonnegative_safe_integer`。
静态 real/logical square CSC 的 `^` 固化 base/result shape、storage 与指数域；JavaScript/C++
分别以自己的 CSC×CSC kernel 执行重复平方法，零次幂产生同阶 canonical double CSC identity，
logical base 提升为 double。两个 planner 独立固定 helper、两组 shape 与三个 integer ABI，renderer
仍只做纯序列化；目标专属 `sparse_power` fragment 仅按需装载并依赖基础 `sparse_matrices` fragment。
逐层/LIR/生成 exponent/storage/shape 计划损坏拒绝、动态非法指数、overflow、双目标差分、source map、
fuzz、架构隔离和第 36 项性能预算共同验证该边界；该阶段的矩形/复数 sparse 与动态 sparse shape 仍失败关闭。
0.7.0 以 Semantic v27、MIR v33 和双目标 LIR v40 将 `SparseValueDomain::finite_complex`
贯穿 constructor、index、mutation、reshape 与 transpose storage lifecycle。静态 binary64 complex
dense/triplet 值直接形成 canonical CSC，重复坐标按 complex addition 聚合并删除 exact-zero
cancellation；`.'` 与 `'` 分别绑定普通和共轭 CSC transpose。JavaScript 使用显式 complex value-domain
tag，C++17 使用 `sparse_matrix<std::complex<double>>`，两目标均以显式 `RuntimeFeature::complex_sparse`
控制组合 runtime fragment，只在程序存在实际 complex CSC 值时装载；无关 complex scalar 与 real CSC
同时存在不会误触发该 fragment。逐层/LIR 损坏拒绝、双目标差分、source map、
nonfinite duplicate runtime 拒绝、fuzz、架构隔离和第 37 项性能预算固定该边界；complex sparse
arithmetic/solve、矩形 sparse solve 与动态 sparse shape 仍失败关闭。
0.7.1 以 Semantic v28、MIR v34 和双目标 LIR v41 为 `SparseArithmeticPlan` 增加显式
`SparseValueDomain`。静态 complex CSC 可与 complex/real/logical CSC、dense matrix 或 scalar
执行 compatible-size `+`/`-`；sparse-sparse 结果保持 canonical complex CSC，mixed 路径直接产生
complex dense result。singleton row/column expansion、`0×n` shaped-empty、双向 complex scalar、
exact-zero cancellation 和 real/logical promotion 均由 Analyzer 预先固定。两个 target planner 独立
序列化第六个 value-domain integer，JavaScript 使用 tagged complex value，C++17 使用类型化
`std::complex<double>`；各自 runtime 验证 domain、CSC、shape、有限输入和有限结果，real-only
sparse arithmetic 继续裁剪 complex runtime。complex sparse product/scale/`.*`/power/solve、
rectangular sparse solve 与动态 sparse shape 仍失败关闭。
同一 LIR v40 还显式保存 Matlab 数组 ownership：dense name-to-name assignment 与需要隔离的
local-function 参数通过 array-copy plan 在共享边界复制一次，sparse indexed assignment/growth/deletion
则返回并替换新 CSC 根值，避免 JavaScript object alias 改写旧值；arrays runtime 依赖闭包和计划损坏
均由 verifier 拒绝。该收尾不宣称已完成一般动态 NDArray/view 的 copy-on-write 或 escape analysis。
一般 NDArray 表示与跨函数动态 shape 数据流、
command form、cell/struct/string 和异常结构仍不在当前可保持边界。因此文档、版本说明和 CLI
必须继续使用“已验证子集”的表述。

## 产品语义边界

Matlab frontend 必须按照 Matlab 语义建立规范事实，不能先生成 JavaScript 再让其他目标
读取 JavaScript。生产链路固定为：

```text
Matlab physical source
  → Matlab logical source / token stream
  → matlab::ast（保留 Matlab 表面语法和版本信息）
  → HIR（规范名称、调用、控制和语言无关操作）
  → MIR（类型化 CFG、值、numeric class/complexity、shape、storage、alias、effect）
  → JavaScript semantic plan / JavaScript LIR
  → renderer（只序列化）
  → JavaScript + source map + dependency manifest + compilation report
```

需要跨语言复用的能力应扩展 HIR/MIR 的强类型 operation 或 side table；Matlab 专属规则应由
language profile 或专属 AST/HIR fact 明确选择。禁止用源拼写字符串、renderer 中的语言猜测、
JavaScript truthiness 或嵌套 Array 的偶然行为代替 Matlab 语义。

## P0：语义可信的 Matlab2JS 核心

P0 完成前，产品定位保持“实验性已验证子集”。以下顺序按语义风险与用户代码出现频率排列。

### Grammar 与表达式

- [ ] 以 R2024a/R2024b 官方 grammar 建立产生式清单、版本 gate、错误恢复和稳定诊断编号
- [x] 让表达式 token/AST/HIR 明确保留 `*`、`/`、`\`、`^` 与 `.*`、`./`、`.\`、`.^` 的不同身份
- [x] 完成共轭转置 `'`、非共轭转置 `.'` 与字符向量引号的表达式级上下文语法
- [ ] 完成关系、逐元素逻辑、短路逻辑、colon、函数/command 调用和转置/字符向量的上下文语法
- [ ] 完成 `switch/case/otherwise` 的 cell case；标量 numeric/logical/character case 已交付
- [ ] 完成 `try/catch`、`return`、`global`、`persistent` 和 `arguments` block 的语法与失败关闭边界
- [ ] 对尚未支持的 `classdef`、并行和工具箱语法提供产生式级诊断，不降级为错误表达式

### 数组与数值语义

- [ ] 建立类型化 NDArray contract：rank/shape/stride/layout、numeric class、值语义、view/owner、copy-on-write 决策
- [x] 实现二维 real/complex 矩阵乘法、相同 shape 逐元素 `+`/`-`/`.*`/`./`/`.\`/`.^` 及 scalar expansion 的首批规则
- [x] 实现 real/complex vector/rank-2 的共轭/非共轭转置；character/string 数组转置仍待建设
- [x] 实现静态满秩稠密实数方阵/超定/欠定矩阵的左除/右除，以及 safe-integer 正/零/负方阵整数幂；双目标使用同一 solve-kind 与失败关闭边界
- [x] 以部分主元 LU 处理方阵，用正/转置求解构造迭代 1-范数 `rcond` 估计；精确奇异与近奇异分别稳定警告并继续产生 Matlab 风格 IEEE 结果
- [x] 以 rank-aware 带列主元 Householder QR 统一处理超定/欠定矩形系统，返回 Matlab 风格基本最小二乘解，非预期数值秩亏继续计算并稳定警告
- [x] 实现首批结构感知 solver dispatch：exact-zero diagonal/upper/lower 分类、直接对角求解、三角前向/回代、dense LU fallback、左右除统一分派及结构对应的条件 warning
- [x] 实现 full-real tridiagonal 相邻行部分主元 LU、正/转置求解，以及 exact-symmetric positive-definite Cholesky；对称不定矩阵安全回退 dense LU
- [x] 实现稠密 complex 方阵 Hermitian-positive-definite Cholesky/dense LU 分派、共轭转置右除、条件 warning 与 safe-integer 正/零/负矩阵幂
- [x] 实现 `~`/`&`/`|` compatible-size N 维逐元素逻辑、scalar `&&`/`||` 短路、condition 非空且全元素非零 truthiness，以及 condition-context scalar `&`/`|` 短路；语义策略贯穿 Semantic v10/MIR v16/LIR v22，并覆盖双目标、拒错、source map、差分、fuzz 和性能
- [x] 固化当前 real scalar `/`、`\`、`./`、`.\` 的 IEEE-754 除零合同；JavaScript 使用 ECMAScript Number 运算，C++17 使用参数化 target runtime helper，惰性分支中的字面量零分母可在 MSVC `/WX` 下编译，且 renderer 不恢复源语义
- [x] 实现静态 shape 及 local-function runtime rank/extent 下逐维相容或一侧 extent 为 1 的隐式扩展，并在 HIR/MIR/目标 LIR 显式保存 shape source 与 broadcast plan
- [x] 实现静态 extent 的 `end` 逐索引位置/线性 numel 语义
- [x] 实现保序/重复 numeric selector、empty selector、线性/逐维 logical selector 和标量/vector 写入；静态可证 shape 在 Analyzer 拒绝，不可静态确定的 logical extent 在生成 runtime 验证
- [x] 实现动态 `end` 的 axis/linear extent 解析；一般动态 bounds 与跨函数 shape 数据流仍待建设
- [x] 实现 vector/matrix/N 维单轴 indexed deletion 与多轴自动 growth；支持 scalar/range/numeric selector、vector 线性语义、运行时标量 selector 和间隙默认值；按 Matlab null-assignment 规则，非 vector 线性删除与多个非 colon selector 属于非法源码并以 `MPF2050` 失败关闭，动态函数参数另在双目标 runtime 复核 vector shape
- [x] 建立 `[]` 的 `0×0` double 语义、静态零 extent array-literal/reshape/transpose/broadcast/section/growth plan 和 JavaScript shape descriptor
- [ ] 建立可跨函数传播不可结构恢复 shape 的统一动态 NDArray ABI，并完成一般 assignment conformability
- [x] 建立 logical/signed-integer/binary64 class 与 real/complex complexity 的正交可验证表示；complex 仅在 binary64 class 上有效
- [x] 建立静态 real/logical/complex rank-2 canonical CSC 表示（包含零 extent）、dense/CSC conversion/query/count 与方阵 solve storage contract；`0×0` 系数的左右除按显式 shape ABI 保持 dense/CSC shaped-empty 结果
- [x] 建立静态 real rank-2 CSC 只读 indexing：linear/subscript scalar、storage-preserving linear/submatrix selection、Matlab shape/orientation、direct CSC、O(nnz) full-colon、零 extent source/result、双目标独立 runtime 与逐层损坏计划拒绝
- [x] 建立静态 real rank-2 CSC reshape：size-vector/维度列表/单 `[]` 推断、列主序顺序保持、N 维请求折叠为二维 sparse 结果、零 extent 保持、O(nnz + result-columns) 双目标直接 CSC runtime 与逐层计划验证
- [x] 建立静态 finite-real rank-2 CSC 矩阵乘法：sparse×sparse→canonical CSC、两类 mixed product→dense，零 extent 结果保形、双目标独立 nonzero-driven runtime、逐层 storage policy、source map、差分、篡改拒错、fuzz 与性能预算
- [x] 建立静态 finite-real rank-2 CSC 逐元素乘法：独立 `SparseElementwisePlan`、scalar/dense/CSC 五种方向、compatible-size 双轴广播、零 extent 与 canonical CSC 结果、双目标 nonzero-driven runtime、逐层损坏拒绝、source map、差分、fuzz 与性能预算
- [x] 建立静态 real/logical square CSC 非负 safe-integer 方阵幂：显式 exponent/storage/shape contract、双目标独立重复平方法、零次幂 identity、logical-to-double promotion、按需 runtime、生成 ABI 拒错、source map、差分、fuzz 与性能预算
- [ ] 完成 single/其余整数 numeric class、NaN/Inf/signed-zero 的全部边界和一般 sparse 表示
- [ ] 对 JavaScript `Number` 无法精确保持的整数、浮点和 complex 行为在 capability 阶段失败关闭或选择 runtime

### 函数、工作区与副作用

- [ ] 区分 script caller/base workspace、普通 function 独立 workspace 和 nested function 共享 workspace
- [ ] 实现 anonymous/nested function、function handle、捕获、逃逸和生命周期
- [ ] 实现 `nargin`/`nargout`、`varargin`/`varargout`、缺省输出、逗号分隔列表和多输出忽略位
- [ ] 建立 `global`/`persistent` 的 module/runtime storage 与初始化顺序
- [ ] 将动态调用、`eval`、`assignin`、`load` 等影响名称/工作区的行为标记为显式 effect 和 capability

### JavaScript runtime

- [ ] 将 Matlab runtime 从每文件内嵌 helper 收敛为版本化、可 tree-shake 的 ESM 包，同时保留无外部依赖模式
- [ ] runtime API 只消费 JavaScript LIR 已规划的类型、布局、ownership 与异常策略，不重新分析源语义
- [ ] 为 dense/sparse/complex/string/cell/struct/function handle 建立稳定内部 ABI 和互操作边界
- [ ] 记录全部 NPM/runtime 组件的版本、来源、完整性、许可证、SBOM 和安全更新策略
- [ ] 为运行时错误建立稳定 Matlab 类别、源位置回映和可诊断的 JavaScript Error 层级

## P1：常用工程代码覆盖

- [ ] char 与 string 的独立类型、Unicode/shape/拼接/比较和 conversion
- [ ] cell 的 `()`/`{}`、comma-separated list、嵌套赋值和展开
- [ ] struct/动态字段、struct array、字段读取写入和 shape
- [ ] function/script/package/class 文件解析，路径解析、私有目录和名称优先级
- [ ] 常用 array/math/statistics/string/IO intrinsic 的分层 catalog 与版本/参数 contract
- [ ] MAT 文件、文本/表格输入输出的可选 runtime adapter
- [ ] table、timetable、datetime、duration、categorical 的首批高频纵切面
- [ ] 对不支持的 toolbox API 提供确定性 capability report，不生成表面可运行但语义错误的代码

## P2：大型工程与对象系统

- [ ] `classdef`、value/handle class、继承、property/method/event、访问控制和生命周期
- [ ] package、namespace、import、class folder、startup/path 和多文件增量 compilation graph
- [ ] MEX/Java/.NET/native library boundary 的明确支持或拒绝策略
- [ ] 并行/GPU/codegen/toolbox 专属语义的插件式 capability 与 runtime provider
- [ ] 项目级缓存、并行 lowering、稳定 artifact schema 和可复现构建

## 每个能力的完成定义

任一复选框只有同时满足以下条件才可标记完成：

1. 记录官方 Matlab 版本、语法和运行语义依据，并给出支持/不支持边界。
2. 正向、边界、负向和版本 gate 用例先落地；无损经过 Matlab AST、HIR、MIR 和 JavaScript LIR。
3. Analyzer 的 type/shape/storage/alias/effect side table 完整，verifier 能拒绝缺失、陈旧和矛盾事实。
4. JavaScript capability/legalization 明确选择 direct、rewrite、runtime 或 unavailable；renderer 不做新决策。
5. 生成代码通过 Node.js 语法和执行测试；source map 能回到原始 `.m` 位置；诊断不输出半成品代码。
6. 授权环境中与目标 Matlab 版本差分；本地 oracle 只能补充，不能替代 reference runner。
7. 覆盖成功/拒绝 fuzz seed、确定性 mutation、资源上限和最小化回归样本。
8. 在真实规模基准上记录延迟、峰值内存、产物/runtime 体积和执行性能，不突破版本化预算。
9. 更新支持矩阵、专项计划、公开示例和用户可见限制；必要时记录 runtime 依赖和许可证。

## 测试与发布门禁

- [ ] 建立可授权的 Matlab runner：固定 release、license/runner 隔离、超时、日志脱敏和结果归一化
- [ ] differential corpus 按 scalar、array、function、workspace、object、error、library 分层，禁止只累计 happy path
- [ ] 每个 P0 operator/index feature 至少覆盖标量、空值、shape 边界、错误类型、求值次数和副作用顺序
- [ ] 增加语法/AST 结构 fuzz、表达式/shape 语义 fuzz、runtime differential fuzz 和历史崩溃 corpus
- [ ] source map 门禁覆盖续行、多语句行、local/nested function、生成 runtime wrapper 和异常栈
- [x] 0.4.8 性能门禁增加 N 维 broadcast、转置、数组比较、逻辑索引和 `end` 的专属编译延迟、吞吐与产物大小预算；0.4.9 增加矩阵 solve/power、逐维 logical 与重复 numeric selector 编译场景；0.5.0 增加动态 `end` 读写和 runtime-shape broadcast 场景；0.5.1 增加 vector/matrix/N 维扩容与删除场景；0.5.2 增加重复零 extent reshape/broadcast/transpose/section/growth 场景；0.5.3 增加 rank/condition-aware solve；0.5.4 增加 diagonal/upper/lower/dense 结构分派、左右除和混合数值矩阵字面量场景；0.5.5 增加 pivoted-tridiagonal、Cholesky、对称不定回退及左右除场景；0.5.6 增加 logical kernel 与 `all`/`any` reduction kernel；0.5.7 增加 complex array/scalar/transpose 与跨函数动态 numeric kernel；0.5.8 增加 complex matrix multiply/Hermitian Cholesky/dense LU/left-right solve/integer-power kernel；0.5.9 增加 complex rectangular CPQR/multi-RHS/left-right solve kernel；0.6.0 增加 sparse CSC conversion/tridiagonal/general-LU square-solve kernel；0.6.1 在同一第 25 项 workload 中加入 zero/inferred/sized/reserved triplet construction、duplicate accumulation、full 与 sparse transpose；0.6.2 增加第 26 项 sparse indexing workload；0.6.3 增加第 27 项 sparse assignment workload；0.6.4 增加第 28 项 sparse reshape workload；0.6.5 增加第 29 项 sparse matrix-product workload；0.6.6 增加第 30 项 sparse scalar-product workload，并以 performance schema v3 为五个重型场景设置独立 latency/throughput/arena/generated-size 预算而不放宽其他 Matlab 场景门槛；0.6.7 增加第 31 项 sparse element-wise workload 及独立四维预算；0.6.8 在同一第 25 项 workload 中加入零维系数、dense/CSC RHS/LHS 与四种 shaped-empty 左右除，不新增或放宽预算；0.6.9 增加第 32 项 logical sparse storage workload 与独立四维预算；0.7.0 增加第 37 项 complex sparse storage lifecycle workload 与独立四维预算；0.7.1 增加第 38 项 complex sparse arithmetic workload 与独立四维预算
- [ ] 性能门禁继续覆盖大 dense array 执行、matrix multiply/section runtime、冷启动和运行时包体积
- [ ] 发布报告自动生成 Matlab feature manifest、reference 版本、差分 case 数、已知限制和性能变化
- [ ] P0 全部完成且连续发布门禁稳定后，才评估从“实验性子集”提升产品成熟度标记

## 当前迭代记录

- [x] 完成专项审计并建立本计划，明确当前不是完整 Matlab 2024 实现
- [x] 交付标量 numeric/logical/character `switch/case/otherwise`，selector 只求值一次且无 fallthrough
- [x] 修正选择语义的源语言耦合：Matlab 字符 case 使用精确相等，Fortran 继续使用补空格比较
- [x] 增加 lexer、双后端、拒绝、差分示例和 Matlab fuzz seed
- [x] 运算符身份与矩阵/逐元素基础 lowering：双目标 LIR helper plan、runtime、正负测试、差分示例和 fuzz seed
- [x] 0.4.8 纵切面：静态 N 维隐式扩展、数组比较、vector/rank-2 转置、上下文 `end`、线性逻辑索引与双目标 runtime
- [x] 为上述能力加入跨层 verifier、五组双目标差分示例、负向语义测试、fuzz seed 和专项性能发布预算
- [x] 0.4.9 纵切面：静态满秩稠密实数方阵/超定/欠定 `\`/`/`、safe-integer 方阵 `^`、部分主元/CPQR 双目标 runtime、MatrixOperationPlan/solve kind 与差分/fuzz/性能门禁
- [x] 0.4.9 索引纵切面：逐下标 selector plan、保序/重复/空 numeric selector、线性/逐维 logical selector、动态 mask runtime 验证与双目标差分/fuzz/性能门禁
- [x] 0.5.0 索引纵切面：静态 `end` 常量快路、动态 axis/linear extent side table、HIR/MIR/LIR verifier、双目标独立 runtime、差分/source map/fuzz/性能门禁
- [x] 0.5.0 广播纵切面：static-extents/runtime-operands shape source、未知 rank 显式计划、双目标矩形 shape 推导、标量/数组泛型调用、关系比较、拒错及差分/source map/fuzz/性能门禁
- [x] 0.5.1 shape-mutation 纵切面：Semantic v7/MIR v12/LIR v18 的 overwrite/resize/grow/erase plan、完整 storage-root memory write、双目标独立 growth/axis-erase runtime、差分/source map/fuzz/性能及损坏事实拒绝门禁
- [x] 0.5.2 empty-array 纵切面：`[]` 规范 `0×0` double、Semantic v8/MIR v13/LIR v19 shaped-empty plan、零 extent reshape/transpose/broadcast/section/growth、JavaScript descriptor、C++ 静态 shape 消费及差分/source map/fuzz/性能门禁
- [x] rank-aware solve 纵切面：矩形 CPQR 基本解、秩亏 warning 与欠定非 minimum-norm 修正
- [x] condition-aware square solve 纵切面：Semantic v10/MIR v16/LIR v22 `MatrixConditionPolicy`、部分主元 LU、迭代 1-范数 `rcond`、singular/nearly-singular warning 后继续、双目标左右除差分、warning 次数/source map/fuzz/性能门禁
- [x] 0.5.4 结构感知方阵纵切面：Semantic v10/MIR v16/LIR v22 `MatrixStructurePolicy`、diagonal/upper/lower/dense 双目标分派、结构对应条件估计、左右除、递归数值字面量 widening、差分/source map/fuzz/性能门禁
- [x] 0.5.5 高级实数方阵纵切面：Semantic v10/MIR v16/LIR v22 `classify_real_square`、带主元三对角 LU、Cholesky、对称不定 dense fallback、转置条件估计、左右除及差分/source map/fuzz/性能门禁
- [x] 0.5.6 逻辑纵切面：operator identity/precedence、`LogicalEvaluation` side table、MIR lazy CFG、双目标 logical/truthiness runtime、N 维 broadcast、source map、拒错、差分、fuzz 与性能门禁
- [x] 0.5.6 逻辑归约纵切面：Matlab 专属 `all`/`any` intrinsic、Semantic v10/MIR v16/LIR v22 `ReductionPlan`、默认首个非 singleton 维、常量 `dim`/`vecdim`、`'all'`、N 维/零 extent shape identity、未知 rank total reduction、双目标列主序 runtime、source map、拒错、差分、fuzz 与性能门禁
- [x] 0.5.6 标量除法可移植性纵切面：HIR v2 `DivisionByZero::ieee754`、MIR v16 传播、双目标 LIR v22 runtime-call plan、C++ direct-division form 移除、MSVC 严格生成编译与 Python 同类异常语义门禁
- [x] 0.5.7 complex 基础纵切面：`NumericClass`/`NumericComplexity` 贯穿语言 AST、Semantic v11、MIR v17、JavaScript LIR v23 与 `cpp` LIR v23；imaginary literal、可遮蔽 `i`/`j`、complex scalar/array、共轭/普通转置、跨函数动态 numeric ABI、双目标独立 runtime、source map、差分、fuzz、损坏事实拒绝与性能门禁完成
- [x] 0.5.8 complex 稠密方阵纵切面：`MatrixNumericDomain::complex`/`classify_complex_square` 贯穿 Semantic v12、MIR v18 与双目标 LIR v24；matrix multiply、Hermitian Cholesky、pivoted dense LU、多 RHS 左除、共轭转置右除、condition warning、safe-integer matrix power、source map、差分、fuzz、runtime 拒错与性能门禁完成
- [x] 0.5.9 complex 矩形求解纵切面：`MatrixFactorizationPolicy::rank_revealing_column_pivoted_qr` 贯穿 Semantic v13、MIR v19 与双目标 LIR v25；超定/欠定、多 RHS、秩亏 basic solution/warning、共轭转置右除、source map、差分、fuzz、损坏计划拒绝与性能门禁完成
- [x] 0.6.0 sparse CSC 方阵纵切面：`ArrayStorageFormat`/`MatrixStoragePolicy` 贯穿 Semantic v14、MIR v20 与双目标 LIR v26；conversion/query/count、三对角快路、一般稀疏行主元 LU、左右除、storage 保持、condition warning、source map、差分、fuzz、损坏事实拒绝和第 25 项性能门禁完成
- [x] 0.6.1 sparse constructor/transpose 纵切面：`SparseConstructionPlan` 贯穿 Semantic v15、MIR v21 与双目标 LIR v27；五类 R2024 调用、zero/empty、推断/显式 shape、scalar expansion、duplicate accumulation/cancellation、`nzmax`、real CSC transpose、source map、差分、生成拒错、fuzz、架构、性能及逐层损坏计划拒绝完成
- [x] 0.6.2 sparse read-only indexing 纵切面：`SparseIndexPlan` 贯穿 Semantic v16、MIR v22 与双目标 LIR v28；scalar/linear/submatrix selection、selector/result shape、CSC storage、alias/effect、纯 Emitter、双目标独立 direct-CSC runtime、source map、差分、runtime 拒错、fuzz、架构与独立性能预算完成
- [x] 0.6.3 sparse indexed assignment 纵切面：`SparseMutationPlan` 贯穿 Semantic v17、MIR v23 与双目标 LIR v29；linear/subscript assignment、linear/axis deletion、dense/sparse RHS、scalar expansion、duplicate last-write-wins、zero erase、静态 growth、自别名、alias/effect、纯 Emitter、双目标独立 transactional CSC runtime、source map、差分、runtime 拒错、fuzz、架构与第 27 项性能预算完成
- [x] 0.6.4 sparse reshape 纵切面：`SparseReshapePlan` 贯穿 Semantic v18、MIR v24 与双目标 LIR v30；size-vector/dimension-list、单 `[]` 推断、输入/请求/折叠结果 shape、CSC storage、alias/effect、纯 Emitter、双目标独立 O(nnz + result-columns) runtime、source map、负向/损坏计划、差分、生成 runtime 拒错、fuzz、架构与第 28 项性能预算完成
- [x] 0.6.5 sparse matrix-product 纵切面：`sparse_csc_multiply` 贯穿 Semantic v19、MIR v25 与双目标 LIR v31；sparse×sparse/sparse×dense/dense×sparse 的 storage、shape、finite-real contract，纯 Emitter、目标独立 nonzero-driven kernel、source map、负向/跨层/LIR 损坏、差分、生成 runtime 篡改拒错、fuzz、架构与第 29 项性能预算完成
- [x] 0.6.6 sparse scalar-product 纵切面：`sparse_csc_scale` 贯穿 Semantic v20、MIR v26 与双目标 LIR v32；双向 operand identity、CSC storage/shape、finite-real contract、纯 Emitter、目标独立 O(nnz) kernel、零因子 O(columns) 空 CSC 快路、source map、负向/跨层/LIR 损坏、差分、生成 nonfinite/overflow runtime 拒错、fuzz、架构与第 30 项性能预算完成
- [x] 0.6.7 sparse element-wise 纵切面：`SparseElementwisePlan` 贯穿 Semantic v21、MIR v27 与双目标 LIR v33；五种 scalar/dense/CSC operand form、compatible-size row/column expansion、canonical CSC result、纯 Emitter、目标独立 nonzero-driven kernel、source map、负向/跨层/LIR 损坏、差分、生成 nonfinite/overflow/计划污染拒错、fuzz、架构与第 31 项性能预算完成
- [x] 0.6.8 静态零 extent sparse 收尾：全部既有 CSC 操作保持显式 shape；双目标 LIR v34 固化 `runtime_shape_arguments`，`0×0` 方阵左右除及 dense/CSC shaped-empty 结果、纯 renderer、source map、跨层/LIR 损坏、双目标差分、生成计划拒错、fuzz、架构与 sparse-solve 性能 workload 完成
- [x] 0.6.9 logical sparse storage 纵切面：`SparseValueDomain`/`SparseDuplicatePolicy` 贯穿 Semantic v22、MIR v28 与双目标 LIR v35；typed logical CSC 构造、duplicate `any`、transpose/index/mutation/reshape/`full` class 保持、纯 Emitter、source map、跨层/LIR 损坏、双目标差分、生成 shape-plan 拒错、fuzz、架构与第 32 项性能预算完成
- [x] 当前开发分支 sparse logical operator 纵切面：独立 `SparseLogicalPlan` 贯穿 Semantic v23、MIR v29 与双目标 LIR v36；static rank-2 `~`/`&`/`|` 的 compatible-size shape、storage policy、纯 Emitter、目标独立 CSC/dense kernel、零 extent、source map、逐层/LIR/生成 ABI 损坏拒绝、双目标差分、fuzz、架构与第 33 项性能预算完成
- [x] 当前开发分支 sparse logical reduction 纵切面：`ReductionStoragePolicy` 贯穿 Semantic v24、MIR v30 与双目标 LIR v37；static real/logical rank-2 CSC `all`/`any` 的 axis/shape/storage、纯 Emitter、按需 runtime fragment、双目标 O(nnz + output-extent) kernel、零 extent、source map、逐层/LIR/生成 ABI 损坏拒绝、双目标差分、fuzz、架构与第 34 项性能预算完成
- [x] 当前开发分支 sparse arithmetic 纵切面：独立 `SparseArithmeticPlan` 贯穿 Semantic v25、MIR v31 与双目标 LIR v38；static real/logical rank-2 CSC `+`/`-` 的 compatible-size shape、storage policy、纯 Emitter、按需 target-owned runtime fragment、sparse-sparse canonical CSC merge、mixed dense materialization、零 extent、source map、逐层/LIR/生成 ABI 损坏拒绝、双目标差分、nonfinite/overflow 拒绝、fuzz、架构与第 35 项性能预算完成
- [x] 当前开发分支 sparse matrix-power 纵切面：`MatrixStoragePolicy::sparse_csc_power` 与 `MatrixExponentPolicy::nonnegative_safe_integer` 贯穿 Semantic v26、MIR v32 与双目标 LIR v39；static real/logical square CSC 的 shape/storage/exponent contract、纯 Emitter、按需 target-owned runtime fragment、双目标 repeated-squaring CSC kernel、zero-power identity、logical-to-double promotion、source map、逐层/LIR/生成 ABI 损坏拒绝、双目标差分、动态非法指数/overflow 拒绝、fuzz、架构与第 36 项性能预算完成
- [x] 0.7.0 complex sparse storage 纵切面：`SparseValueDomain::finite_complex` 贯穿 Semantic v27、MIR v33 与双目标 LIR v40；complex dense/triplet constructor、duplicate sum/cancellation、普通/共轭 transpose、index/mutation/growth/reshape/`full`、纯 Emitter、由显式 feature 精确按需装载的组合 runtime fragment、source map、逐层/LIR 损坏、双目标差分、nonfinite duplicate 拒绝、fuzz、架构与第 37 项性能预算完成
- [x] 0.7.1 complex sparse arithmetic 纵切面：`SparseArithmeticPlan::value_domain` 贯穿 Semantic v28、MIR v34 与双目标 LIR v41；complex/real/logical CSC、mixed dense、双向 complex scalar、compatible-size expansion、零 extent、canonical complex CSC、exact-zero cancellation、目标独立 runtime 与按需 complex feature 完成
- [ ] 随后纵切面：sparse rectangular solve、complex sparse matrix-product/scalar-product/element-wise/power/solve、动态 sparse shape、病态特定解精确对齐、其余 numeric class、跨函数动态 NDArray shape ABI

## 官方语义索引

- [Array vs. Matrix Operations](https://www.mathworks.com/help/matlab/matlab_prog/array-vs-matrix-operations.html)
- [`plus` / addition](https://www.mathworks.com/help/matlab/ref/double.plus.html)
- [`mtimes` / matrix multiplication](https://www.mathworks.com/help/matlab/ref/double.mtimes.html)
- [`times` / element-wise multiplication](https://www.mathworks.com/help/matlab/ref/double.times.html)
- [`mpower` / matrix power](https://www.mathworks.com/help/matlab/ref/mpower.html)
- [Sparse matrix operations](https://www.mathworks.com/help/matlab/math/sparse-matrix-operations.html)
- [`all` logical reduction](https://www.mathworks.com/help/matlab/ref/all.html)
- [`any` logical reduction](https://www.mathworks.com/help/matlab/ref/any.html)
- [Operator Precedence](https://www.mathworks.com/help/matlab/matlab_prog/operator-precedence.html)
- [Short-Circuit AND](https://www.mathworks.com/help/matlab/ref/shortcircuitand.html)
- [Short-Circuit OR](https://www.mathworks.com/help/matlab/ref/shortcircuitor.html)
- [`if` condition semantics](https://www.mathworks.com/help/matlab/ref/if.html)
- [`while` condition semantics](https://www.mathworks.com/help/matlab/ref/while.html)
- [Solve Systems of Linear Equations with `mldivide`](https://www.mathworks.com/help/matlab/ref/double.mldivide.html)
- [`mrdivide` matrix right division](https://www.mathworks.com/help/matlab/ref/double.mrdivide.html)
- [`mpower` matrix power](https://www.mathworks.com/help/matlab/ref/double.mpower.html)
- [`linsolve` matrix-property options](https://www.mathworks.com/help/matlab/ref/linsolve.html)
- [`rcond` Reciprocal Condition Number](https://www.mathworks.com/help/matlab/ref/rcond.html)
- [Compatible Array Sizes for Basic Operations](https://www.mathworks.com/help/matlab/matlab_prog/compatible-array-sizes-for-basic-operations.html)
- [Array Indexing](https://www.mathworks.com/help/matlab/math/array-indexing.html)
- [Detailed Rules About Array Indexing](https://www.mathworks.com/help/matlab/math/detailed-rules-about-array-indexing.html)
- [Detailed Rules About Indexed Assignment](https://www.mathworks.com/help/matlab/math/detailed-rules-about-indexed-assignment.html)
- [Accessing Sparse Matrices](https://www.mathworks.com/help/matlab/math/accessing-sparse-matrices.html)
- [Find Array Elements That Meet a Condition](https://www.mathworks.com/help/matlab/matlab_prog/find-array-elements-that-meet-a-condition.html)
- [Empty Arrays and Null Assignment](https://www.mathworks.com/help/matlab/math/empty-arrays.html)
- [`end`](https://www.mathworks.com/help/matlab/ref/end.html)
- [`switch`](https://www.mathworks.com/help/matlab/ref/switch.html)
- [`sparse` matrix construction](https://www.mathworks.com/help/matlab/ref/sparse.html)
- [`reshape`](https://www.mathworks.com/help/matlab/ref/double.reshape.html)
- [Sparse reshape remains two-dimensional](https://www.mathworks.com/matlabcentral/answers/98101-why-am-i-unable-to-use-the-reshape-function-to-convert-a-sparse-vector-to-a-three-dimensional-matrix)
- [Complex conjugate transpose](https://www.mathworks.com/help/matlab/ref/double.ctranspose.html)
- [Command vs. Function Syntax](https://www.mathworks.com/help/matlab/matlab_prog/command-vs-function-syntax.html)
- [Base and Function Workspaces](https://www.mathworks.com/help/matlab/matlab_prog/base-and-function-workspaces.html)
- [Local Functions](https://www.mathworks.com/help/matlab/matlab_prog/local-functions.html)
- [Data Types](https://www.mathworks.com/help/matlab/data-types.html)
