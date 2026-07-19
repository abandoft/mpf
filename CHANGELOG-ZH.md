## 0.6.2

- Matlab 稀疏矩阵现支持通过一个线性 index 或两个行/列 subscript 进行只读 scalar 访问。
- sparse selection 支持 full colon、正/负步长 slice、保序或重复 numeric selector、logical selector 与 empty selector。
- 线性 sparse 结果遵循 Matlab shape 规则：numeric matrix selector 保持自身 shape，logical matrix 与 full colon 产生 column，vector/vector indexing 保持 source orientation。
- 双下标 selection 按 Matlab 语义形成 Cartesian product，结果 shape 为 `numel(rows) × numel(columns)`。
- 非标量 sparse indexing 保持 canonical CSC storage，且不会物化源矩阵的稠密副本。
- full-colon selection 以 O(nnz) 直接 remap CSC 条目；submatrix selection 通过有序 row map 扫描选中的列。
- 生成的 JavaScript 与 C++17 使用彼此独立、带检查的 sparse-index runtime；任一目标均不依赖另一后端产物。
- 新增类型化 `SparseIndexPlan`，使 scalar/selection identity、输入/结果 shape 与 source/result storage 贯穿语义分析、MIR、JavaScript LIR 和 `cpp` LIR。
- 跨层 verifier 会在发射前拒绝损坏的 sparse-index identity、arity、shape、type、storage 或 inactive-state fact。
- 当 storage representation 已知时，`full` 现可接受结果 extent 为动态值或零值的 rank-two dense/CSC selection。
- sparse assignment、超过两个 selector、complex/sparse selector、N 维 linear result，以及动态、空或 complex sparse source 继续以稳定诊断失败关闭。
- 新增可执行 sparse-indexing 示例，覆盖双目标行为、source-map 保留、越界拒绝与 fuzz 回归；生产代码行覆盖率为 91.24%（33,780/37,023）。

## 0.6.1

- Matlab 现可在 MPF 的非空、静态 shape、real rank-2 contract 内接受 R2024 全部 `sparse` 调用形式：`sparse(A)`、`sparse(m,n)`、推断 shape 的 triplet、显式 shape triplet 与 `nzmax` 形式。
- `sparse(m,n)` 与显式 shape 的空 triplet 现可直接创建全零 canonical CSC 矩阵，不会物化稠密存储。
- triplet 构造支持 row index、column index 或 value 的 scalar expansion，同时要求所有非标量输入具有相同元素数量。
- 推断式 triplet 构造从正的编译期 literal index 得到输出维度；显式 shape 构造会按请求 shape 验证已知及运行时 index。
- 重复 triplet 坐标会按列主序确定性累加；精确抵消后的条目不会进入存储。
- `nzmax` 会作为非负编译期整数验证，并在生成 C++17 中作为 capacity hint 使用，不改变矩阵可观察内容。
- 生成的 JavaScript 与 C++17 会从 triplet 直接建立有序 canonical CSC 数据，不再分配中间稠密矩阵。
- 普通转置与共轭转置现可在已交付的 real sparse contract 内保持 CSC 存储，并调用目标专属 transpose helper。
- 新增类型化 `SparseConstructionPlan`，使 constructor identity、结果 shape、triplet cardinality 与 reserve intent 贯穿语义分析、MIR、JavaScript LIR 和 `cpp` LIR。
- 独立 HIR、MIR、JavaScript LIR 与 `cpp` LIR verifier 会在发射前拒绝损坏的 sparse arity、shape、cardinality、reserve、storage 或 inactive-state fact。
- 稀疏构造与转置经过两个目标专属 runtime call 后仍保留 source location；任一输出后端均不依赖另一后端产物。
- runtime 现以稳定诊断拒绝小数或非正 triplet index、非标量 triplet 数量不一致、越界坐标、非法维度和非有限重复项累加。
- 新增可执行、差分、生成 runtime 拒错、fuzz、架构与性能覆盖，固定 zero、empty、inferred、sized、reserved、scalar-expanded、duplicate 与 transposed sparse matrix 行为。

## 0.6.0

- Matlab `sparse(A)` 现可把非空、静态 shape 的 real rank-two 数组转换为 canonical compressed sparse column 存储。
- Matlab `full`、`issparse` 与 `nnz` 现支持本版本交付的 dense/CSC 转换、存储查询和非零计数流程。
- 稀疏实数方阵左除现可在生成的 JavaScript 与 C++17 中处理向量和多列右端项。
- 三对角稀疏系数使用紧凑 O(nnz) 分解；其他方阵结构使用带部分行主元的稀疏 LU，且不会创建稠密系数副本。
- 稀疏方阵右除通过转置稀疏求解复用同一算法，并按 Matlab 左/右操作数规则保持稀疏结果。
- 精确奇异与近奇异稀疏系统现会产生和既有稠密方阵求解器一致的稳定条件 warning。
- 生成的 JavaScript 使用私有 tag 的 CSC 值，生成的 C++17 使用类型化 `mpf_runtime::sparse_matrix`；两套 runtime 继续彼此独立。
- runtime 会拒绝畸形 CSC pointer、无序 row index、零值或非有限存储项，以及不匹配的求解 shape。
- 生成的 C++17 sparse validation 可通过严格 GCC dangling-reference 分析，且不需要压制编译器诊断。
- 尚未支持的 sparse constructor、索引、转置、reshape、逻辑、逐元素、乘法、幂、矩形、复数和零 extent 情形会在发射前以 `MPF2054` 失败。
- 数值类型规划现可保持 Matlab binary64 数组，同时保证 Python operand-returning 短路与条件表达式在生成 C++17 中得到正确结果类型。
- 新增可执行稀疏求解与条件 warning 示例，并覆盖 source map、fuzz 和双目标差分验证。

## 0.5.9

- Matlab 复数矩形左除现可在生成的 JavaScript 与 C++17 中处理超定和欠定系统。
- 复数矩形右除对两种求解 shape 均遵循 Matlab 的共轭转置求解恒等式。
- 多列右端项现共享一次复数列主元 QR 分解。
- rank-revealing 复数 Householder QR 使用确定性列主元和 working-precision 秩容差。
- 秩亏复数系统会返回 pivoted 基本最小二乘解并产生稳定 warning，不再静默失败。
- 矩阵计划现通过语义分析、MIR 和双目标 LIR 显式携带 rank-revealing 列主元 QR 分解策略。
- Semantic、MIR、JavaScript LIR 与 `cpp` LIR 调试 schema 提升到 v13、v19、v25，并拒绝损坏的分解策略。
- JavaScript 与 C++17 继续使用独立复数矩形 runtime，并验证有限值、矩形性、shape 和求解计划。
- source map 保留复数超定、欠定、左除和右除运算符的原始位置。
- 新增复数矩形与秩亏 Matlab 可执行示例，并进行双目标差分和 warning 验证。
- 新增复数矩形 fuzz 覆盖和专用编译性能 workload。

## 0.5.8

- Matlab 二维复数矩阵现可在生成的 JavaScript 与 C++17 中执行真正的矩阵乘法。
- 稠密复数方阵左除现支持向量或多列右端项。
- 复数方阵右除按共轭转置求解恒等式执行，不再复用实数转置语义。
- exact Hermitian 正定系数矩阵现使用专用复数 Cholesky 分解与求解路径。
- 非 Hermitian 或非正定复数方阵会确定性回退到按模选主元的稠密 LU。
- 复数方阵求解器现估计倒条件数，并保持既有精确奇异与近奇异 warning 行为。
- 复数方阵幂通过平方求幂支持零、正、负 ECMAScript-safe integer 指数。
- 生成 runtime 会在各自边界拒绝畸形、非有限、shape 不一致或分数指数的复数矩阵操作。
- 矩阵计划现通过所有编译层显式携带实数/复数 numeric domain 与独立 complex-square 结构策略。
- Semantic、MIR、JavaScript LIR 与 `cpp` LIR 调试 schema 提升到 v12、v18、v24，并验证 numeric domain 与损坏事实。
- JavaScript 与 C++17 使用彼此独立的复数矩阵 runtime，任一目标都不读取另一目标的生成代码或 runtime 产物。
- complex-matrix runtime fragment 会验证依赖，并在纯实数矩阵程序中完全裁剪。
- source map 保留复数矩阵乘法、左右除及正/负矩阵幂的原始位置。
- 新增 Hermitian、稠密换行主元、精确奇异和近奇异复数矩阵可执行示例及双目标差分验证。
- 新增复数矩阵 fuzz seed，以及动态非法矩阵幂指数的生成 runtime 拒错覆盖。

## 0.5.7

- Matlab 现支持以 `i` 或 `j` 结尾的虚数数值字面量；预定义虚数单位名称仍是可被正常遮蔽的普通标识符。
- 新增 Matlab 标量 `complex`、`conj`、`real`、`imag`，以及支持复数的 `abs` 运算。
- 复数标量算术现支持加、减、乘、稳定缩放除法、一元正负号和包含零指数的整数幂。
- compatible-size 复数数组算术现支持 scalar expansion 和 N 维逐元素运算，并可生成两个输出目标。
- Matlab 普通转置 `.'` 与共轭转置 `'` 现对实数/复数 vector 和 rank-two 数组保持各自不同的行为。
- 复数值经过索引赋值与 `reshape` 后会继续保持数值身份和列主序数组语义。
- Matlab 局部函数可传递和返回运行时才确定为实数或复数的标量/数组，无需先执行 JavaScript 生成流程。
- 生成的 JavaScript 使用独立且带检查的复数 runtime；纯实数程序不会携带任何复数 helper。
- 生成的 C++17 使用独立的 `std::complex` runtime，并可推导动态数值结果类型，不依赖 JavaScript 产物。
- 尚未支持的复数比较、逻辑、归约、矩阵求解/乘法/幂会在发射前以稳定的 `MPF2053` 诊断失败。
- 数值类别及实数/复数身份现贯穿语义分析、MIR 和两个目标专属 lowering 管线进行验证，并覆盖损坏事实拒绝测试。
- source map 现可保留复数构造、算术、数组写入、reshape 和转置操作的源码位置。
- 新增可执行 Matlab 复数示例、双目标差分覆盖、生成代码断言和专用 fuzz 回归 seed。
- Python 标量返回注解现会约束生成的 C++17 类型，避免带整数注解的函数产生不兼容推导类型。
- 扩展全量测试发现的 Fortran optional 可写参数和 Python truthiness 回归已在两个输出后端修复。

## 0.5.6

- Matlab 的 `~`、`&`、`|`、`&&` 与 `||` 现会保持各自的优先级和求值行为，不再被当作可互换的布尔运算符。
- 逐元素逻辑运算现支持 scalar expansion 和 compatible-size N 维 numeric/logical 数组，并可生成 JavaScript 与 C++17。
- 标量短路表达式在两个输出目标中都惰性求值，且每个操作数最多求值一次。
- Matlab `if`/`while` condition 现遵循“非空且所有元素非零”的数组 truth 语义；有歧义的非标量 condition composition 会失败关闭。
- Matlab `all`/`any` 现支持默认首个非 singleton 维、常量 `dim`、常量 `vecdim` 与 `'all'`。
- 逻辑归约会保持 vector、matrix 和 N 维数组的结果 rank/extent，也正确处理高于输入 rank 的维度。
- 空归约会保持准确的 `0×N`/`N×0` shape，并在对应位置使用 Matlab 的 `all=true`、`any=false` identity。
- rank 只能在运行时确定的 local function 现可使用 `all(values,'all')` 和 `any(values,'all')`，无需静态容器 shape。
- 生成的 JavaScript 使用带检查的列主序归约 kernel，并通过已有的不可枚举 shape descriptor 保持零 extent。
- 生成的 C++17 使用独立、类型化的递归 `std::vector` 归约 kernel，并可通过严格 C++17 零警告编译。
- 逻辑求值与归约 contract 会在语义分析、MIR、JavaScript LIR 和 `cpp` LIR 中独立验证后才允许发射代码。
- 标量除法现通过 HIR、MIR 和双目标 LIR 显式携带除零策略；生成的 C++17 通过可移植目标 runtime 保持 Matlab/TypeScript IEEE 结果，Python 真除法与 floor division 则在两个目标中给出稳定错误。
- source map 现覆盖逻辑与归约 runtime 调用；字符数组、动态维度、重复/非法维度和不支持的未知 rank 归约会给出稳定诊断。
- 新增双目标可执行示例、差分 case、跨层损坏检查、runtime 断言和 Matlab 专用 fuzz seed。

## 0.5.5

- Matlab 方阵左除和右除现可识别完整实数三对角系数矩阵，对齐 Matlab R2024a 为稠密输入引入的结构能力。
- 三对角系统现使用相邻行部分主元 LU 与紧凑对角存储，不再进入一般稠密 LU。
- 三对角 runtime 支持多列右端项和专用转置求解，因此右除复用同一特化契约。
- 精确对称正定矩阵现由生成的 JavaScript 与 C++17 使用 Cholesky 分解和三角替换求解。
- 不是正定的对称矩阵会确定性回退到部分主元稠密 LU，不会暴露失败的 Cholesky 中间结果。
- 两个输出目标现按对角、三角、三对角、对称正定及稠密回退的固定优先级选择方阵结构。
- 三对角与 Cholesky 内核现进入统一的迭代 1-范数倒条件估计，用于方阵条件 warning。
- 精确奇异三对角系统和近奇异正定系统会给出稳定的 Matlab 风格 warning，并继续返回计算结果。
- 可验证矩阵结构策略现通过 Semantic、MIR、JavaScript LIR 和 `cpp` LIR 表达完整实数方阵分类。
- 生成 helper 名称现描述实数结构化方阵契约，不再保留早期仅含对角/三角含义的身份。
- 新增可执行的三对角左/右除、正定、对称不定、奇异及近奇异示例，并验证双目标输出和 warning。
- 扩充高级结构路径的 source map 断言、verifier 损坏检查、架构检查和 Matlab fuzz seed。

## 0.5.4

- Matlab 方阵左除与右除现会在运行时识别对角、上三角、下三角和一般稠密实数系数矩阵。
- 对角系统现使用直接逐元素求解，不再分配并分解稠密 LU 矩阵。
- 上三角与下三角系统现分别在生成的 JavaScript 和 C++17 中使用专用前向或回代。
- 一般方阵继续使用带部分主元的稠密 LU 作为确定性回退路径。
- Matlab 右除会对转置后的除数分类，与左除使用同一结构感知求解契约。
- 倒条件估计现跟随所选对角、三角或稠密 kernel，并包含估计器所需的转置求解。
- 精确奇异和近奇异的结构化系统会给出已有的稳定 Matlab 风格警告，并继续返回计算得到的 IEEE 结果。
- 生成 C++17 现会递归统一混合整数与浮点矩阵各行的数值容器类型。
- 矩阵结构策略会在语义分析、MIR、JavaScript LIR 与 `cpp` LIR 中逐层验证后才允许发射代码。
- source map 现可保留结构感知左除与右除表达式的原始源码位置。
- 新增结构化求解和条件警告可执行示例、双目标差分 case、verifier 损坏测试与 Matlab fuzz 回归种子。

## 0.5.3

- Matlab 矩形左除与右除现对超定和欠定系统统一使用 rank-aware 带列主元 QR。
- 欠定求解现返回 pivoted basic solution，不再产生此前不正确的最小范数结果。
- 数值秩亏的矩形系统会给出稳定的工作精度警告，并继续返回基本最小二乘解。
- 精确奇异方阵的左除与右除现会警告并继续，保留已验证的 Matlab 风格有限分量和 IEEE 无穷分量，不再终止生成程序。
- 近奇异方阵现复用 LU 因子估计倒条件数，给出独立的 `RCOND` 警告，并继续返回计算结果。
- 矩阵条件行为以可验证的 `MatrixConditionPolicy` 贯穿语义分析、MIR、JavaScript LIR 与 `cpp` LIR。
- 生成的 JavaScript 与 C++17 现分别拥有独立的部分主元 LU、转置求解、条件估计和带列主元 QR runtime。
- 非 vector 线性删除继续被一致拒绝；奇异方阵执行不再被错误归类为不支持的 runtime 操作。
- source map 现可保留 condition-aware 方阵左除与右除的源码位置。
- 生成 C++ 现会在除法前拒绝不可能成立的零 extent 坐标转换，使空数组输出可在 MSVC `/WX` 下保持零警告。
- 发布 SHA-256 侧车文件现使用无回车格式，即使软件包在 Windows 上构建，也可由标准 Unix 校验工具直接验证。
- 新增精确奇异和近奇异 Matlab 可执行示例，并验证两个目标的输出与警告次数。
- 扩充 conditioned solve 的跨层损坏检查、生成代码断言、架构门禁与 Matlab fuzz corpus。

## 0.5.2

- Matlab `[]` 现在使用规范的 `0×0` double-array shape，不再被当作 rank-one 空 list。
- `reshape` 现在接受零 extent 并保持 `0×5` 等 shape，包括无法仅从嵌套容器结构恢复的维度。
- 转置会保持准确的零 extent shape，使 `0×N` 与 `N×0` 在生成的 JavaScript 和 C++17 中继续可区分。
- 空数组与 scalar 的算术和关系比较现在会保持完整的广播结果 shape 与元素类型。
- 空 selector 和 colon section 读取会保持静态已知的结果 rank 与零 extent。
- 对 `[]` 的线性赋值遵循 Matlab row-vector 扩容语义；从有 shape 的空矩阵扩容时则保持已规划的维度和列主序布局。
- 生成的 JavaScript 使用经过检查且不可枚举的 descriptor 保存准确数组 shape，不改变普通数组迭代和序列化行为。
- 生成的 C++17 现直接消费目标后端拥有的静态 shape plan，用于 rank、`length`、转置、广播和扩容，不依赖 JavaScript 输出。
- 矛盾或损坏的空数组 shape fact 会在目标发射前及生成 runtime 边界被一致拒绝。
- 新增双目标可执行空数组示例、source map 检查、跨层损坏拒绝测试、fuzz 回归 seed 和专用性能发布场景。

## 0.5.1

- Matlab 索引赋值现可通过 scalar、colon range 和保序 numeric selector 扩容 row vector、column vector、matrix 与 N 维数组。
- 线性扩容保持 Matlab 列主序和 vector 方向；矩阵/张量按需扩展末维，并用元素类型默认值初始化中间空位。
- Matlab `[]` 赋值现可删除 vector 元素或 matrix/tensor 的一个选定维，支持 scalar、slice、numeric 与 logical selector，重复位置只删除一次。
- shape-changing write 同时支持静态边界、运行时索引、local-function 参数和动态 `end`，生成的 JavaScript 与 C++17 分别独立执行。
- Analyzer 所有的 `IndexedMutationContract` 显式记录 overwrite、resize、grow、erase、线性布局、删除轴、shape 来源以及输入/结果 shape。
- Semantic、MIR 与目标 LIR schema 分别升级到 v7、v12 和 v18；每一层都会在发射前验证 mutation rank、方向、axis 与 shape 一致性。
- MIR 将 growth/deletion 记录为整个 storage 的写入；memory-dependence 在形成必要依赖后裁剪被覆盖的同根历史，避免沿用旧局部区域及 frontier 二次增长。
- 生成 JavaScript 使用带安全检查的嵌套数组 resize/axis deletion；生成 C++17 使用类型化嵌套 `std::vector` 模板，不消费 JavaScript 产物。
- 两个 runtime 在各自边界验证安全 size、selector bounds/type、矩形 rank、replacement cardinality 和有歧义的多维删除。
- source map 已覆盖 growth/deletion 调用；线性 matrix 删除、多轴删除和越界删除以稳定诊断失败关闭。
- 新增静态及运行时 shape 的可执行差分、N 维生成代码检查、跨层 plan 损坏拒绝和 Matlab 专用 fuzz seed。

## 0.5.0

- Matlab compatible-size 算术和关系比较现支持 rank 与 extent 只能在 local function 实例化或执行时取得的操作数。
- 运行时广播分派可同时保持标量结果、scalar expansion、row-vector 规范化、缺失尾随 singleton 维和一般矩形嵌套数组。
- HIR、MIR、JavaScript LIR 与 `cpp` LIR 现在以可验证的 `static_extents` 或 `runtime_operands` 保存广播 shape 来源；未知 rank 不再伪装成静态空 shape。
- 生成的 JavaScript 在列主序 flatten/stride 内核前一次性推导并验证矩形 operand shape，不兼容 extent 以稳定 runtime 错误失败。
- 生成的 C++17 将模板期 rank 与运行时 extent 组合，返回正确的标量或嵌套 `std::vector` 类型，并独立拒绝 ragged 或不兼容操作数。
- C++ logical array 的 `sum` 现在返回数值计数，不再把全部非零计数折叠为 `true`。
- Matlab `end` 现可用于数组 extent 只能在运行时确定的场景，包括 local function 参数和组合 selector。
- 运行时 `end` 支持列主序线性索引、逐维索引、colon bound、`end - 1` 等算术，以及 `[1 end]` 形式的 numeric selector array。
- 动态 `end` 的 scalar element 与 N 维 section 读取和写入现可在生成的 JavaScript 与 C++17 中独立执行。
- 静态 extent 继续使用编译期常量快路径，固定 shape 索引不会承担运行时 extent 解析成本。
- HIR、MIR、JavaScript LIR 与 `cpp` LIR 现在保存并验证 runtime-axis 或 runtime-linear extent plan，Emitter 不再推断源语言语义。
- Semantic、MIR 与目标 LIR 调试 schema 分别升级到 v6、v11 和 v17，并公开广播 shape 来源与逐 selector extent identity。
- 生成的 JavaScript 按当前轴长度或列主序元素总数解析 selector closure，同时保证被索引容器只求值一次。
- 生成的 C++17 使用类型化 selector callable 和通用列主序 element accessor，并以无 unevaluated lambda 的类型探针保持严格 C++17。
- 新增跨层 plan 损坏拒绝、source map 断言、生成代码检查，以及覆盖动态 `end` 读写的双目标可执行差分示例。
- 新增动态 `end` 专用 fuzz seed 和编译性能场景；函数内没有兼容写入时会裁剪只读 memory frontier，并继续使用现有发布阈值。
- 新增标量/数组动态广播的双目标可执行差分、跨层损坏拒绝、source map 检查、fuzz seed 和专用编译性能场景。

## 0.4.9

- Matlab 矩阵左除现可求解静态满秩稠密实数方阵、超定和欠定系统，并支持一列或多列右端项。
- Matlab 矩阵右除现支持 row vector 或 rank-2 左操作数，以及静态满秩稠密实数方阵或矩形除数。
- Matlab 矩阵幂现支持静态方阵稠密实数矩阵的零次、正整数次和负整数次幂，指数限制在 ECMAScript safe-integer 范围内。
- 方阵求解使用部分主元选取；矩形最小二乘和最小范数求解在生成的 JavaScript 与 C++17 中均使用带列主元的 Householder QR。
- 数组除以 scalar 和 scalar 左除数组现可保持 Matlab 矩阵运算符语义，无需改写为逐元素运算符。
- Matlab 索引新增保序 numeric selector array、重复下标、空 selector，以及线性或逐维位置的 logical selector。
- shape 只能在 runtime 确定的 logical selector 现在由生成代码验证，不再要求所有 mask 大小都能在编译期求出。
- 矩阵运算种类、求解类别及输入/输出 shape 现在以显式、可验证事实贯穿 HIR、MIR、JavaScript LIR 和 `cpp` LIR。
- Semantic、MIR 与目标 LIR schema 分别升级到 v4、v9 和 v15；每个 scalar、slice、numeric、logical 或 empty selector 都拥有逐下标可验证身份。
- 秩亏系统、非方阵幂、矩阵指数、非有限求解值，以及不安全或非整数指数会确定性失败关闭。
- 新增可执行的方阵和矩形求解示例、双目标差分验证，以及秩亏系统的生成 runtime 拒绝门禁。
- 扩充 Matlab 矩阵求解/幂和通用索引的 fuzz、source map、生成代码、runtime 拒绝及编译性能覆盖。

## 0.4.8

- Matlab 数组算术现支持静态已知 N 维 shape 的 compatible-size 隐式扩展，包括 singleton 维和缺失的尾随维度。
- Matlab 关系运算现可生成布尔数组，并支持标量、完全相同 shape 和 compatible-size 扩展，可直接表达 `A(A >= 30)`。
- JavaScript 数组扩展采用一次展平和预计算 stride 的内核；标量及完全相同 shape 的运算继续使用直接快速路径。
- 生成的 C++17 增加目标独立的类型化 N 维扩展和数组比较实现，与 JavaScript 输出保持一致。
- Matlab 共轭转置 `'` 与非共轭转置 `.'` 现在独立解析，并支持当前实数 vector 和 rank-2 数组。
- Matlab `end` 可在静态已知 extent 时按当前索引维度或线性元素总数解析，并可参与算术和 colon 表达式。
- Matlab 逻辑 mask 现支持列主序线性读取，以及标量或 vector 写入，并严格检查 mask 大小和 replacement shape。
- 动态 extent、高 rank 转置、不兼容 mask、通过 `end` 扩容、矩阵除法和矩阵幂会在生成代码前使用专用诊断失败关闭。
- 新增五个可执行 Matlab 示例和差分 case，双目标覆盖隐式扩展、转置、`end`、逻辑索引和广播比较。
- 扩充 Matlab fuzz 回归输入，并加强 broadcast plan、转置身份、逻辑选择和优化后中间表示的跨层验证。

## 0.4.7

- Matlab 现在区分矩阵运算符与 `.*`、`./`、`.\`、`.^` 等逐元素运算符，不再在解析阶段丢失语义。
- 新增二维 numeric 矩阵乘法，并同时生成可执行的 JavaScript 与 C++17 实现。
- 新增同 shape 数组及 scalar expansion 的 `+`、`-`、`.*`、`./`、`.\`、`.^` 基础运算。
- shape 不相容或尚未支持的矩阵除法、矩阵幂会给出 `MPF2046`，不会生成不可靠的目标代码。
- Matlab 运算符身份使用强类型事实贯穿 AST、HIR、MIR 与目标 LIR，便于后续扩展广播、转置和数值类型。
- JavaScript 与 C++ 后端源码改为独立语言目录，文件名移除重复目标前缀，新增目标时边界更清晰。
- 新增 Matlab 数组运算差分示例、正负语义测试、目标代码检查和 fuzz 回归样本。
- Matlab → JavaScript 语言支持矩阵、诊断文档和项目路线图已同步当前能力与已知限制。
- C++ 输出现在为词法作用域中同名的 symbol 分配不同目标标识符，在保持源语言作用域语义的同时避免 MSVC 警告升级为错误。

## 0.4.6

- CMake 安装包改为精确版本匹配；使用方需要通过 `find_package(mpf 0.4.6 EXACT ...)` 查找当前版本。
- 源语言名称统一为 `matlab`、`python`、`fortran`、`typescript`，输出目标统一为 `javascript`、`cpp`。
- 移除 `py`、`f90`、`ts`、`js`、`c++` 等名称缩写；文件扩展名自动识别不受影响。
- C++ API 的语言名称和版本解析接口现在使用 `std::optional` 表示无效输入，不再静默选择默认语言或目标。
- 前端和后端扩展接口升级至 API v6，并移除旧名称别名字段。
- CMake 包只公开标准 component 状态和导出目标，移除早期开发快照中的大写后端状态变量。
- 诊断文本和 JSON 现在始终提供完整源码范围，便于编辑器准确标记问题位置。
- 安装示例、嵌入示例、扩展指南和版本文档已同步到当前接口。

## 0.4.5

- 改进分支、循环和函数调用中的内存依赖分析，使数组及 section 写入顺序更加可靠。
- 能够区分不相交的 N 维区域，减少对安全访问的保守限制。
- 编译报告新增内存依赖统计，便于定位复杂控制流和潜在性能问题。
- 优化大型控制流图和依赖去重，相关基准场景耗时由约 354 ms 降至约 43 ms。
- 加强循环携带依赖、未知外部调用和索引写入的回归与 fuzz 验证。

## 0.4.4

- 更精确地跟踪变量、数组元素、section、参数复制和写回产生的读写行为。
- 跨函数调用传播参数的实际访问区域，改善可写参数和数组 section 的正确性检查。
- 改进别名分析，可识别明确不相交、明确相同和无法确定的内存区域。
- 修复优化后内存访问信息可能与生成代码不同步的问题。
- 扩充索引、切片、写回和跨函数数组访问的稳定性测试。

## 0.4.3

- 新增统一的 N 维数组区域模型，支持连续区间、步长 section、负步长和多维矩形区域。
- Fortran 现在允许把同一数组中可证明互不重叠的多个 section 作为可写实参传入过程。
- 对真实重叠或边界无法确定的可写实参继续给出安全诊断。
- 改进空 section、大步长区域和列主序线性选择的处理。
- 新增 Fortran 不相交区域示例及跨 gfortran、JavaScript、C++ 的结果验证。

## 0.4.2

- 在 JavaScript 和 C++ 后端之前加入共享优化流程，两个目标使用同一份优化结果。
- 新增安全的整数与布尔常量折叠，并避免超出 JavaScript 精确整数范围时改变程序语义。
- 规范化静态数组 shape，清理无效指令和简单冗余控制流。
- 改进块参数复制传播，同时保留一般分支合流和动态值语义。
- 编译报告新增优化前后规模、常量折叠和清理统计。
- 新增 Python 优化示例，并验证源程序、JavaScript 和 C++ 输出结果一致。

## 0.4.1

- TypeScript `let` 和 `const` 支持嵌套块作用域、同名遮蔽及对外层变量赋值。
- TypeScript 新增标准 C 风格 `for` 循环，支持初始化、条件、更新、`break` 和 `continue`。
- 修复生成 JavaScript/C++ 时局部变量作用域和生命周期不准确的问题。
- TypeScript `number` 按 ECMAScript 数值语义处理，数组索引拒绝无法安全表示的非整数值。
- JavaScript 输出只携带当前程序实际需要的运行时辅助代码。
- 新增 TypeScript 块作用域和 `for` 循环的多目标执行验证。

## 0.4.0

- 新增 TypeScript 前端，可通过 `.ts`、`.mts` 和 `.cts` 文件自动识别。
- 首批支持 `let`/`const`、标量与类型化数组、赋值、函数、默认参数、条件、`while`、循环控制和 `console.log`。
- 支持擦除当前子集中的类型标注，并生成 JavaScript 或 C++17。
- TypeScript `export function` 可生成对应的 JavaScript ESM 导出。
- 对 `var`、宽松相等、箭头函数、模板字符串和尚未建模的对象语义给出明确诊断，避免生成行为不一致的代码。
- 新增 Node.js 源码、生成 JavaScript、生成 C++ 和声明结果的四路验证。

## 0.3.9

- 改进条件表达式、Python `and`/`or` 和比较链的短路求值，确保操作数只在需要时执行。
- 变量读取、初始化、赋值、索引写入和参数写回采用统一的中间表示，减少两个后端之间的行为差异。
- 修复可写 section 参数在 copy-in、copy-out 和 writeback 阶段的副作用跟踪。
- 加强函数参数、返回 shape、动态 extent 和惰性分支的验证。
- 改善大型程序的中间表示查询和验证性能。

## 0.3.8

- 重构编译器中间表示，降低大型表达式树和控制流的遍历成本。
- JavaScript 与 C++ 后端改为从统一的值、类型、shape 和存储信息生成代码。
- 改进损坏或不完整中间状态的检测，避免错误继续传播到代码生成阶段。
- 本版本不扩大语言语法范围，重点提升编译稳定性和后端一致性。

## 0.3.7

- Python、Matlab 和 Fortran 解析器改为直接构建各自的语言 AST，减少重复复制和峰值内存。
- 改进三种语言的错误恢复，解析失败后不会发布不完整语法节点。
- 不同语言的 AST 现在相互独立，为后续扩展完整官方 grammar 提供清晰边界。
- 移除不再使用的共享语法入口，降低新语言接入时的耦合。

## 0.3.6

- Python 新增 `is`、`is not`、`in` 和 `not in`。
- 改进布尔、数值、字符串、list 和 tuple 的相等比较语义。
- 支持字符串、list 和 tuple membership，以及混合 comparison chain。
- JavaScript 输出能够区分 list 与 tuple，并保持支持范围内的引用身份。
- C++ 输出新增递归容器比较和 membership 支持；无法可靠表达的 identity 比较会明确拒绝。
- 新增 Python 比较语义的 CPython、JavaScript 和 C++ 结果验证。

## 0.3.5

- 改进跨函数类型、shape、tuple 返回值和引用参数的检查。
- 更准确地处理只读借用、可写借用、copy-out、copy-in/out、optional 转发和省略参数。
- 修复函数调用中的数组 section 写回、可选参数转发和潜在别名问题。
- JavaScript 与 C++ 后端分别规划函数 ABI、临时值、声明顺序和模块布局，生成任一目标不再依赖另一目标。
- 改善大型函数图和复杂调用场景的分析性能与错误诊断。
- 加强资源上限检查，避免参数展开后绕过节点数量限制。

## 0.3.4

- 静态数组对象语义扩展到一般 N 维声明、嵌套 shape、`RESHAPE`、索引和 section 读写。
- 新增三维 Fortran tensor 示例，并验证 gfortran、JavaScript 和 C++ 输出一致。
- 公共 API 和 CLI 新增源语言版本选择；不支持的版本特性会给出明确诊断。
- 新增源码映射输出、依赖清单、资源限制和机器可读编译报告。
- 前端、共享分析和目标后端改为清晰分层的编译管线，JavaScript 与 C++ 后端保持独立。
- GitHub 自动化按验证、平台、质量、Sanitizer、覆盖率、性能、安全和发布职责拆分。
- 增加 fuzz、性能基线、安装后消费和外部扩展示例。

## 0.3.3

- 前端和后端改为可查询的 descriptor/registry 架构，便于增加新的源语言和输出目标。
- 公共 API 可查询已构建的源语言和目标后端。
- 内置函数绑定由各语言和目标显式声明，缺少目标实现时会在生成代码前报错。
- 禁用某个后端时仍可查询其元数据，且不会编译或链接该后端实现。
- 新增前端、后端和内置函数绑定的扩展指南。

## 0.3.2

- Python 新增链式比较和右结合条件表达式。
- 比较链保持从左到右、单次求值和短路行为。
- 条件表达式在 JavaScript 与 C++ 输出中保持惰性分支和 Python truthiness。
- 对 C++ 无法静态表达的比较或分支结果给出明确诊断。
- 新增 CPython、JavaScript 和 C++ 四路结果验证。

## 0.3.1

- Fortran 新增 `SELECT CASE`、`CASE DEFAULT` 和 `END SELECT`。
- 支持 integer、character 和 logical selector，以及单值、闭区间和省略边界的区间。
- 新增区间类型、反向区间和重叠 CASE 检查。
- character CASE 按 Fortran 空格补齐规则比较。
- 改进多分支后的确定赋值和终止流分析。

## 0.3.0

- 新增统一的 C++ 代码格式化和静态检查配置。
- 新增源码覆盖率报告，并设置 85% 的生产代码行覆盖率门槛。
- GitHub 自动化加入格式、静态检查、覆盖率、CodeQL 和依赖安全检查。
- 修复质量检查发现的重复分支和无效状态更新。
- 所有质量报告继续统一生成到 `build/` 目录。

## 0.2.9

- Python 解包赋值支持嵌套 tuple/list pattern 和 starred target。
- 支持 star 位于任意位置、空 capture、嵌套 capture 和重复名称覆盖。
- 解包右侧只求值一次，并在 JavaScript 与 C++ 输出中保持赋值顺序。
- 对动态长度、结构不匹配和 C++ 无法表示的异质 star 结果给出明确诊断。
- 新增 CPython、JavaScript 和 C++ 四路解包验证。

## 0.2.8

- C++ 输出目标的统一名称改为 `cpp`。
- 公共 API 使用 `TargetLanguage::cpp`，CLI 使用 `--target cpp`。
- CMake 后端开关、组件名和导出目标同步使用 `cpp`。
- 生成代码标准仍为 C++17；标准版本不再出现在目标身份中。
- 这是 0.x 阶段的命名变更，旧名称不再保留。

## 0.2.7

- Python 新增平坦 tuple/list 解包赋值。
- 支持裸 `a, b`、圆括号、方括号和单目标尾随逗号。
- 支持 tuple-return 函数、交换赋值、异质 tuple 和重复目标。
- JavaScript 与 C++ 输出均保证右侧表达式只求值一次。
- 动态未知长度或目标数量不匹配时会给出明确诊断。

## 0.2.6

- Python 函数支持标量默认参数。
- 新增 positional-only `/`、keyword-only `*`、关键字实参和尾随逗号。
- 调用分析能够补齐默认值，并检查重复、未知、缺失及位置错误的实参。
- 当前默认值限于无副作用的不可变标量；容器、调用和标识符默认值会明确拒绝。
- JavaScript 与 C++ 输出使用相同的参数关联结果。

## 0.2.5

- Fortran optional 参数扩展到当前支持范围内的标量和数组 `INTENT(IN/OUT/INOUT)`。
- 支持 optional 可写实参、数组元素、整数组和一/二维 section 的有序写回。
- optional 参数跨过程转发时能够保持缺省状态或引用身份。
- 修复缺省 optional OUT 参数被错误视为未初始化读取的问题。
- JavaScript 与 C++ 输出新增对应的 optional reference 运行时支持。

## 0.2.4

- Fortran 新增关键字实参和 `OPTIONAL` 声明属性。
- 支持标量 `OPTIONAL, INTENT(IN)`、`PRESENT` 和 optional 参数转发。
- 调用时检查未知关键字、重复关联、缺少必需参数及 positional-after-keyword。
- JavaScript 使用 `undefined` 表示缺省实参，C++ 使用类型化 optional 值。
- 尚未支持的 optional 数组和可写 optional 参数会明确拒绝。

## 0.2.3

- Fortran 可写过程实参扩展到数组元素和一/二维 section。
- 连续与非连续 section 通过 copy-in/copy-out 保持写回语义。
- Fortran function 的返回值与 section 写回按正确顺序执行。
- 对可能重叠的多个可写实参采用保守诊断，避免生成不安全代码。
- 新增 gfortran、JavaScript 和 C++ 的 section 实参验证。

## 0.2.2

- Fortran 支持一维和二维 assumed-shape dummy array。
- 调用时检查标量/数组分类、rank、静态 extent 和元素类型。
- C++ 输出使用引用共享数组存储，JavaScript 输出保持数组 OUT 参数的原位更新。
- 非 dummy assumed-shape 声明和尚不安全的非连续可写 section 会明确拒绝。

## 0.2.1

- Fortran 新增 `INTENT(OUT)` 和 `INTENT(INOUT)`。
- OUT/INOUT 实参按引用传递，并在调用后更新调用方变量。
- 调用分析允许未初始化 OUT，要求 INOUT 已赋值，并检查所有返回路径。
- 拒绝把同一变量同时绑定到多个潜在写回参数。
- JavaScript 与 C++ 后端分别使用 reference box 和 C++ 引用保持语义。

## 0.2.0

- Fortran 新增 function、subroutine、`RESULT`、`RETURN`、`RECURSIVE` 和 `CALL`。
- 支持内部/外部过程、dummy parameter、提前返回和具名 `END` 校验。
- 支持递归标量 function 和递归 subroutine。
- 区分 function expression 与 `CALL` subroutine 的使用位置，并对混用给出诊断。
- 在完整引用参数支持前，可能被错误按值传递的 dummy 修改会被安全拒绝。

## 0.1.9

- 改进函数依赖分析，支持前向调用、直接递归和互递归。
- 函数返回类型、元素类型和 shape 可沿调用链传播。
- C++ 输出会按依赖顺序生成函数，并为可静态描述的递归函数建立前置声明。
- 无法为 C++ 建立可靠返回类型的递归函数会明确拒绝，JavaScript 输出仍可独立使用。
- 改进 Matlab 多输出值跨函数转发。

## 0.1.8

- Matlab 新增 `[a, b] = f(...)` 多输出赋值。
- Matlab 多输出函数在单值上下文中按语言规则选择第一个输出。
- JavaScript 使用数组与解构，C++ 使用 `std::tuple` 与 `std::get`。
- 两个后端都保证多目标赋值的右侧只求值一次。
- 新增输出数量、重复目标和非法右侧检查。

## 0.1.7

- Fortran 前端改用 token 化 statement lexer 和递归下降 parser。
- 支持当前 program、声明、IF、DO、循环控制、PRINT/WRITE、CALL 和赋值子集。
- free/fixed source form 均使用新的解析流程。
- 修复合法实体名被错误识别为关键字的问题。
- 修复标准 `WRITE(*,*) value` 解析，并保留常见带逗号形式。

## 0.1.6

- Matlab 前端改用 token 化 statement lexer 和递归下降 parser。
- 支持当前函数、多输出、条件、循环、display、赋值、索引赋值和表达式语句子集。
- 正确区分字符向量、共轭转置和非共轭转置。
- 改进 Matlab 函数返回类型传播和 C++ 前向调用生成。
- 新增 parser 错误恢复和 statement-token 结果验证。

## 0.1.5

- Python 前端改用 token 化 statement lexer 和递归下降 parser。
- 支持当前函数、条件、`while`、`for ... else`、return、循环控制、赋值和 print 子集。
- 表达式继续由统一的优先级 parser 处理，避免 statement 与 expression 语法不一致。
- 新增非法链式赋值、参数形态和孤立 clause 的诊断与恢复。
- 新增 CPython、JavaScript 和 C++ 的 statement-token 验证。

## 0.1.4

- Python 支持括号内隐式续行、反斜杠续行、tab 缩进和一行多个 simple statement。
- Matlab 支持 `...` 续行、多行矩阵、block comment 和一行多个 statement。
- Python 与 Matlab 的注释和字符串现在能够安全跨越逻辑行处理。
- 新增未闭合 delimiter、字符串、注释和错误续行的诊断。
- 新增多行源码的 JavaScript/C++ 结果验证。

## 0.1.3

- Python 条件和循环支持数字、字符串、list、`None` 与 NaN truthiness。
- Python `and`/`or` 保留操作数返回值、短路顺序和左操作数单次求值。
- JavaScript 与 C++ 输出保持支持范围内的惰性求值。
- Python `float` 新增数字、布尔和字符串基础转换，以及 NaN/Infinity 解析。
- C++ 无法静态统一逻辑表达式结果时会在生成前给出诊断。

## 0.1.2

- 建立统一的差分测试语料清单。
- 同一示例可直接比较源语言、生成 JavaScript、生成 C++ 和声明结果。
- 生成 C++ 使用与顶层项目相同的编译器和生成器进行真实编译执行。
- 失败时保留生成源码、编译日志和各执行路径结果。
- 自动化环境固定 Python 和 Node.js 版本，避免运行时缺失导致测试被静默跳过。

## 0.1.1

- Fortran 新增独立的 free-form 和 fixed-form 源码规范化。
- free form 支持 `&` 续行、续行间注释、字符常量续行和分号 statement。
- fixed form 支持 label field、第 6 列 continuation、第 7–72 列 statement 和传统整行注释。
- 根据文件扩展名自动选择 source form，也可通过 API 或 `--fortran-form` 显式指定。
- 新增孤立续行、未完成续行、错误字符续行和不安全 fixed-form 布局诊断。

## 0.1.0

- 新增多文件源码管理和稳定的源码位置标识。
- 所有诊断现在包含文件名、起止位置和源码片段。
- 公共 API 新增诊断文本渲染和 JSON diagnostics v1 输出。
- CLI 新增 `--diagnostics-format text|json`。
- 统一 CLI 退出状态，区分编译、参数、输入和输出错误。
- 安装 diagnostics v1 JSON Schema，并补充工具集成文档。

## 0.0.9

- Python 支持普通切片的可变长度替换和 extended slice 等长赋值。
- Matlab 支持行、列、矩形 block 和列主序线性 section 赋值。
- Fortran 支持一/二维 array section 赋值、shape 检查和标量扩展。
- JavaScript 与 C++ 运行时新增对应的原位 section 更新。
- 对临时 section 写入、shape 不匹配和 C++ 无法保持的动态容器变化给出诊断。

## 0.0.8

- JavaScript 与 C++ 后端拆分为独立组件，可分别启用或禁用。
- 生成任一目标不再要求构建另一目标后端。
- 公共 API 新增后端可用性查询；请求未构建后端时返回明确诊断。
- CMake 安装包新增 `core`、`javascript` 和 `cpp` component。
- 增加 javascript-only、cpp-only 和 core-only 的构建、安装及外部消费验证。
- 修复 Python ragged list 的 rank 信息丢失问题。

## 0.0.7

- Python 新增正负步长切片和更深的矩形嵌套 list shape 推导。
- Matlab 新增整行、整列、二维 block、步长 colon 和 `A(:)`。
- Fortran 新增一/二维 array section、默认边界和正负 stride。
- JavaScript 与 C++ 运行时新增通用 section 读取支持。
- 改进空 section、动态 extent、逐维边界和固定 shape 赋值检查。

## 0.0.6

- Python 支持矩形嵌套 list 和二维索引读写。
- Matlab 支持矩阵字面量、二维索引和列主序线性索引。
- Fortran 支持二维常量 extent、rank 检查及一/二维 `RESHAPE`。
- JavaScript 与 C++ 运行时新增多下标、递归聚合和列主序 reshape。
- C++ 对 ragged list 给出安全诊断。

## 0.0.5

- Python 新增一维 list 索引读写、负下标、`len` 和 `sum`。
- Matlab 新增行向量、1-based 索引、`length`、`numel` 和 `sum`。
- Fortran 新增一维定长数组、数组构造器、1-based 索引、`SIZE` 和 `SUM`。
- 新增静态 extent、同质元素、常量越界和索引类型检查。
- JavaScript 与 C++ 运行时新增对应的边界和索引基准处理。

## 0.0.4

- 新增 Python `elif`、Matlab `elseif` 和 Fortran `ELSE IF`。
- 新增 Python/Matlab `break`、`continue` 与 Fortran `EXIT`、`CYCLE`。
- Python 新增 `for ... else` 和 `while ... else`，包括嵌套循环中的正确 `break` 行为。
- 改进循环与函数上下文检查、不可达代码警告和返回路径分析。
- C++ 输出会拒绝不兼容返回类型及值返回与隐式空返回混用。

## 0.0.3

- 新增名称绑定、内置名称遮蔽、未定义标识符和确定赋值分析。
- 新增整数、实数、布尔和字符串基础类型推导。
- 新增 Python `range`/`while`、Matlab colon `for`/`while`、Fortran `DO`/`DO WHILE`。
- 保持三种语言各自的循环结束变量语义，并支持负 step。
- JavaScript 与 C++ 输出新增保留字和名称冲突安全改写。
- C++ 生成代码隔离到 `mpf_generated` namespace。

## 0.0.2

- 公共 API 和 CLI 新增 JavaScript/C++ 双输出目标选择。
- 新增独立 C++17 后端、基础运行时和可执行入口生成。
- 新增 UTF-8 源码位置、CRLF 处理和公共 token 模型。
- Python、Matlab 和 Fortran 表达式改用结构化 lexer/parser。
- JavaScript 输出新增安全的运算符优先级、Python floor division 和 list/tuple 支持。
- 新增生成 JavaScript 的 Node.js 验证和生成 C++ 的真实编译执行测试。

## 0.0.1

- 建立 MPF 公共 API、命令行工具、三语言基础标量转译、JavaScript 后端、测试和构建基础。
