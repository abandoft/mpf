# MPF 持续建设路线图

本路线图记录 **0.6.1 当前开发基线** 与后续交付目标的真实状态。历史交付细节见
[CHANGELOG-ZH.md](CHANGELOG-ZH.md)，当前可依赖的语言子集见
[docs/LANGUAGE_SUPPORT.md](docs/LANGUAGE_SUPPORT.md)。目标版本号表示语法/语义覆盖上限，不表示已经完整兼容 Matlab 2024、Python 3.14、Fortran 2023 或 TypeScript 6；TypeScript 已有独立、可执行且包含 lexical block/canonical `for` 的子集，但完整 grammar 仍未完成。

前后端商业级重构的权威设计见 [docs/COMPILER_PIPELINE.md](docs/COMPILER_PIPELINE.md)。路线图中的“多层 IR”只能指语言 AST、HIR、MIR、JavaScript LIR/`cpp` LIR 五层均拥有独立强类型数据模型、verifier 和实际生产调用路径；不能用共享结构体 alias、stage flag 或未被 pipeline 消费的空壳类型标记完成。

## 当前基线

| 项目 | 当前开发分支状态 |
|---|---|
| 实现与构建 | CMake 3.20+；配置固定 C17/C++17；当前生产源码和公共 API 使用 C++17，尚无稳定 C API |
| 输出目标 | 独立 JavaScript 与 `cpp` 后端；源码分别位于无重复文件名前缀的 `src/backends/javascript/`、`src/backends/cpp/`，`cpp` 当前生成严格 C++17 translation unit |
| 前后端边界 | 四语言 parser session 直接构造并发布各自 arena AST artifact，不经过共享递归 syntax tree 或整树复制；生产驱动随后固定经过 HIR→MIR→共享优化→优化后 alias/effect→CFG memory-dependence→目标私有 semantic plan/LIR→emitter，两个目标不读取彼此产物 |
| 扩展架构 | frontend descriptor API v6、backend descriptor API v6；仅接受 canonical 语言/目标名称，不保留旧名称 alias；parser session/feature/resource contract、configuration/runtime supply-chain manifest、AST verifier、TargetProfile、稠密 legalization、opaque artifact 和前后端 conformance harness 已接入 |
| IR 架构 | 四种语言使用编译期互不兼容的 PMR arena AST，并原子 lowering 到窄 HIR v2 与 revision-checked 稠密 semantic side table；名称、控制流、alias/effect 和 memory-dependence 分别由独立表持有。MIR v21 使用强类型稠密 expression/statement/instruction arena、显式 CFG、resident instruction 与 revision-bound attributes；Semantic v15→MIR v21→双目标 LIR v27 逐层验证 numeric class/complexity、dense/CSC array storage、matrix numeric domain/solve/condition/factorization/structure/storage、sparse construction、shape/broadcast/reduction/index/mutation、storage region、call ownership/writeback 和 source segment plan。JavaScript LIR 与 `cpp` LIR 各自完成 representation/runtime/module 规划，emitter 仅序列化，任一目标都不读取另一目标产物。 |
| Python 最新能力 | relational/equality/identity/membership 比较链、右结合条件表达式、短路/惰性/单次求值；list/tuple 种类相等规则、singleton/reference identity、string/list/tuple membership；基础参数关联和递归固定序列解包 |
| 跨语言标量除法 | HIR v2 profile 独立保存 quotient 与 zero-denominator policy；Python `/`/`//` 在 JavaScript/C++17 中 checked exception，Matlab/TypeScript 保持 IEEE-754；C++ 统一经过参数化 runtime helper，消除 MSVC 对惰性字面量零除的编译期拒绝；Fortran 当前保持 target-native |
| Matlab 最新能力 | `~`/`&`/`|` 的 compatible-size N 维逐元素逻辑、`&&`/`||` 标量短路、condition 全元素非零/非空 truthiness 与 condition-context scalar `&`/`|` 短路；`all`/`any` 支持默认首个非 singleton 维、常量 `dim`/`vecdim`、`'all'`、N 维 shape 与空归约 identity；`2i`/`3j` 及可遮蔽 builtin `i`/`j`、complex scalar 算术/幂/一元正负、标量 `complex`/`conj`/`real`/`imag`/`abs`、complex 数组 compatible-size 逐元素运算、索引/写入/reshape，以及 `'` 共轭转置与 `.'` 普通转置；跨 local-function 的未知 real/complex 参数由动态 numeric ABI 分派；scalar numeric/logical/character `switch/case/otherwise`；规范 `0×0` double empty、一般静态零 extent reshape/transpose/broadcast/section/growth；R2024 全部 `sparse` 调用形式在非空静态 real rank-2 contract 内的 canonical CSC 构造、scalar expansion、duplicate accumulation、显式 sparse transpose、`full`/`issparse`/`nnz` 及实数方阵稀疏系数左右除；二维矩阵乘法、静态稠密实数 diagonal/upper/lower/pivoted-tridiagonal/symmetric-positive-definite/dense 结构感知方阵、复数 Hermitian Cholesky/dense LU 方阵及 real/complex rank-aware 超定/欠定 solve、safe-integer 方阵 power、静态 N 维及 local-function runtime rank/extent 的 compatible-size 算术/关系比较、静态及运行时 extent 的逐维/线性 `end`、保序/重复/空 numeric selector、线性/逐维 logical selector，以及 vector/matrix/N 维多轴自动扩容与单轴索引删除进入双目标专属 LIR/runtime |
| Fortran 最新能力 | integer/character/logical `SELECT CASE`、范围/default、重叠检查和任意分支确定赋值合流；已知静态 shape 下可证明不相交的同根连续、步长与 N 维矩形 writable section actual |
| 工程门禁 | 237 项内部测试；91 个差分 case、227 条工具完整环境执行路径；Debug/Release/RelWithDebInfo 109 项 CTest；四语言 fuzz smoke、可选 libFuzzer、7 项生成 runtime 拒绝测试、发布脚本正/负契约、25 项独立版本化通用及 Matlab 专项性能阈值、逐 pass/优化/内存依赖统计报告；0.6.1 生产代码行覆盖率 91.10%（32,991/36,213），当前硬门槛 85%；Release 在标签 SHA 上复用七类 required workflow，门禁后才允许三平台候选测试/安装/消费/归档、来源证明和公开资产回验 |
| 发布状态 | 0.x 开发快照；包消费要求精确当前版本，不提供旧 MPF API/ABI/schema/CLI/CMake 兼容承诺或迁移 shim |

## 本轮商业级收尾验收（完成）

- [x] 四语言生产 artifact 使用编译期互不兼容的 PMR arena AST、稠密 ID、verifier、确定性 dump 和 AST→HIR visitor
- [x] 当前支持的分支、循环、loop-else、`break`/`continue` 与 `SELECT CASE` 使用 MIR basic block、terminator、block argument/edge actual；shape stride、storage view/lifetime/intent 可验证，独立 alias/effect table 提供保守 `alias_between` 查询
- [x] JavaScript/`cpp` representation、ABI、type/shape、名称、runtime 与 binding 决策在目标 lowering/renderer 完成；emitter 只序列化最终 chunk
- [x] 公共 output bundle 提供代码、source map v3、确定性 dependency manifest 与逐阶段 `CompilationReport`；CLI 支持 `--source-map`
- [x] 四语言/双目标 fuzz smoke、Clang/libFuzzer、资源耗尽、确定性重放、崩溃复现与最小化工作流落地
- [x] 延迟、吞吐、深 CFG、大 shape、函数图、八路并发、峰值 arena 和产物大小纳入版本化 JSON 发布门禁与 CI 报告

这里的“完成”只指上述架构与工程闭环；各语言官方 grammar、完整对象模型、跨语言动态 shape 传播与 NDArray 表示、一般 view/pointer 的完整 overlap/alias 和稳定插件 ABI 仍由后续条目跟踪。

## 当前迭代优先级

已完成事项由 [CHANGELOG-ZH.md](CHANGELOG-ZH.md) 和架构文档记录；TODO 只保留仍需实施、仍需验证或需要持续维护的工作，不再按旧 MPF 版本复制完成项。当前优先顺序为：

- [x] 删除旧 MPF 包版本范围兼容、历史 consumer 版本请求、旧式包变量、descriptor 名称别名和歧义解析 API；安装包只接受当前精确版本，canonical 名称解析失败时显式返回空值
- [x] Matlab P0 首批运算符 identity、二维矩阵乘法、同 shape/标量逐元素算术、双目标 runtime、失败关闭诊断和差分门禁
- [x] 完成 [Matlab → JavaScript 产品计划](docs/MATLAB_TO_JAVASCRIPT.md) 的 0.4.8 纵切面：静态 `end`、单 mask 线性逻辑索引、静态 N 维隐式扩展、数组比较和 vector/rank-2 转置
- [x] Matlab P0 静态满秩稠密实数方阵/超定/欠定左除与右除、safe-integer 正/零/负整数幂、跨层 MatrixOperationPlan/solve kind、部分主元和 CPQR 双目标 runtime
- [x] Matlab P0 广义索引：逐下标 selector identity、保序/重复/空 numeric selector、线性/逐维 logical selector、动态 mask runtime 验证及双目标门禁
- [x] Matlab P0 动态 `end`：runtime-axis/runtime-linear typed plan 贯穿 HIR/MIR/双目标 LIR，覆盖 scalar/colon/算术/numeric selector 及读写、source map、差分、fuzz 和性能门禁
- [x] Matlab P0 动态 compatible-size：static-extents/runtime-operands typed plan 贯穿 HIR/MIR/双目标 LIR，local function 参数支持标量或运行时 rank/extent 的算术、关系比较及 scalar expansion，并覆盖拒错、source map、差分、fuzz 和性能门禁
- [x] Matlab P0 shape mutation：overwrite/resize/grow/erase 强类型 contract 贯穿 Semantic/HIR/MIR/双目标 LIR；覆盖 vector、matrix、一般 N 维多轴扩容与单轴删除、列主序线性扩容、运行时标量 selector、空值填充、source map、差分、fuzz、性能和损坏事实拒绝
- [x] Matlab P0 空数组静态纵切面：`[]` 规范为 `0×0` double，零 extent 贯穿 reshape/transpose/broadcast/section/growth；Semantic v8、MIR v13 和双目标 LIR v19 保存并验证 array-literal/shape plan，JavaScript 以不可枚举 descriptor 保持不可结构恢复的 shape，C++ 消费静态 shape plan
- [x] Matlab P0 condition-aware 求解：矩形 `\`/`/` 使用列主元 QR 基本最小二乘解并在秩亏时警告；方阵使用部分主元 LU、迭代 1-范数 `rcond` 估计，在精确奇异/近奇异时分别警告并继续产生 IEEE 结果；`MatrixConditionPolicy` 当前贯穿 Semantic v15/MIR v21/双目标 LIR v27
- [x] Matlab P0 首批结构感知方阵分派：`MatrixStructurePolicy` 当前贯穿 Semantic v15/MIR v21/双目标 LIR v27；exact-zero diagonal/upper/lower 使用直接或三角求解及对应条件估计，其余方阵回退 dense LU，左右除、source map、差分、fuzz、性能与损坏事实拒绝均进入门禁
- [x] Matlab P0 高级实数方阵分派：`classify_real_square` 当前贯穿 Semantic v15/MIR v21/双目标 LIR v27；full tridiagonal 使用相邻行部分主元紧凑 LU，非三对角 exact-symmetric positive-definite 使用 Cholesky，对称不定安全回退 dense LU，正/转置求解、条件 warning、左右除、source map、差分、fuzz 和性能均进入门禁
- [x] Matlab P0 逻辑纵切面：lexer/AST 分离 `&`/`|` 与 `&&`/`||` 并遵循官方 precedence；`LogicalEvaluation` 当前在 Semantic v15/MIR v21/双目标 LIR v27 固化 eager-elementwise/boolean-short-circuit/operand-short-circuit，`~`/`&`/`|` 支持 compatible-size N 维逻辑值，condition 使用非空且全元素非零规则；双目标 runtime、source map、拒错、差分、fuzz 和性能门禁完成
- [x] Matlab P0 逻辑归约纵切面：`all`/`any` 绑定为 Matlab 专属 intrinsic；`ReductionPlan` 当前在 Semantic v15/MIR v21/双目标 LIR v27 固化 operation、默认/显式/全维 axis policy、static/runtime shape source、输入/输出 shape 与 scalar identity；默认首个非 singleton 维、常量 `dim`/`vecdim`、`'all'`、N 维及零 extent identity 已由双目标独立列主序 runtime、source map、拒错、差分、fuzz 和第二十一项性能场景验证
- [x] 商业级标量除法纵切面：`Division` 与 `DivisionByZero` 当前经 HIR v2/MIR v21/双目标 LIR v27 传播并独立验证；Python true/floor division 使用双目标 checked runtime，Matlab/TypeScript C++ 使用 portable IEEE helper，renderer 仅序列化 target plan；MSVC、惰性零除、损坏合同和两项生成 runtime 拒错进入门禁
- [x] Matlab P0 complex 基础纵切面：`NumericClass`/`NumericComplexity` 权威 side table 贯穿语言 AST、HIR、Semantic v15、MIR v21 与双目标 LIR v27；支持 imaginary literal、可遮蔽 `i`/`j`、complex scalar/array 逐元素语义、索引写入/reshape、共轭/普通转置及跨函数动态 numeric 分派；JavaScript object ABI 与 C++ `std::complex<double>` runtime 完全独立，并覆盖损坏事实拒绝、source map、差分、fuzz、feature pruning 和第二十二项性能场景
- [x] Matlab P0 complex 稠密方阵纵切面：`MatrixNumericDomain::complex` 与 `classify_complex_square` 贯穿 Semantic v15/MIR v21/双目标 LIR v27；二维矩阵乘法、Hermitian 正定 Cholesky、按模选主元 dense LU、共轭转置右除、singular/nearly-singular warning 及 safe-integer 正/零/负方阵幂由独立目标 runtime、source map、差分、fuzz、拒错和第二十三项性能场景验证
- [x] Matlab P0 complex 矩形求解纵切面：`MatrixFactorizationPolicy::rank_revealing_column_pivoted_qr` 贯穿 Semantic v15/MIR v21/双目标 LIR v27；超定/欠定、多 RHS、秩亏 basic solution 与 warning、共轭转置右除由 JavaScript/C++ 独立 runtime、source map、差分、fuzz、损坏事实拒绝和第二十四项性能场景验证
- [x] Matlab P0 sparse CSC 方阵求解纵切面：`ArrayStorageFormat` 与 `MatrixStoragePolicy::sparse_csc_coefficient` 贯穿语言 AST、Semantic v15、MIR v21 与双目标 LIR v27；`sparse(A)`/`full`/`issparse`/`nnz`、三对角 O(nnz) 快路、一般稀疏行主元 LU、正/转置条件估计、左右除及 RHS/LHS 结果 storage 保持由独立 JavaScript/C++ runtime 实现，并进入 source map、差分、fuzz、拒错、损坏事实和第二十五项性能门禁
- [x] Matlab P0 sparse constructor/transpose 纵切面：R2024 的五类 `sparse` 调用形式由 `SparseConstructionPlan` 在 Semantic v15、MIR v21 和双目标 LIR v27 固化 arity、result shape、triplet cardinality 与 reserve hint；zero/empty、推断/显式 shape、scalar expansion、重复项累加与抵消、`nzmax`、real CSC 普通/共轭转置由双目标直接 CSC runtime、source map、差分、生成拒错、fuzz、架构、性能和逐层损坏事实拒绝验证
- [x] 按 Matlab null-assignment 规则固定删除边界：只允许 vector 线性删除，或恰好一个非 colon 维度的整 slice 删除；非 vector 线性删除和多个非 colon selector 是源语言非法语义，不再列为未来能力
- [ ] 继续 Matlab P0：一般 sparse index/reshape/logical/element-wise/multiply/power/rectangular/complex、零 extent 与动态 shape 语义，病态方阵特定解选择的精确 Matlab 对齐、其余 numeric class，以及可跨函数传播不可结构恢复 shape 的统一动态 NDArray ABI；R2024 full symmetric-indefinite 已按当前 dense LU 行为处理，不把已移除的 LDL 路径列为兼容前提
- [ ] 按 Python/Fortran/TypeScript 官方 grammar 选择下一批可独立验收的纵切面
- [ ] 继续完成跨语言动态 shape 数据流、类型化 NDArray/typed-array ownership、跨一般 view/pointer 的 region/alias 证明
- [ ] 在已交付的区域化 memory-dependence contract 上建立 memory version、memory phi、rename/def-use 的完整 MemorySSA；以负向 verifier、差分、fuzz 和性能门禁后再启用 region-aware DCE/store forwarding
- [ ] 将 interner、诊断、配置和全部阶段 arena 所有权迁入 `CompilationSession`，并让 IR 容器实际使用 session resource
- [ ] 完成稳定 C17 插件 ABI 前保持 descriptor 为源码树内的精确当前契约，不提供旧 descriptor adapter、size negotiation 或跨版本动态加载
## M0：工程与端到端基础（完成）

- [x] C17/C++17 标准配置、CMake 3.20+ 和严格根目录 `build/` 构建边界
- [x] 分层源码目录、公共 C++ API 与 `mpfc` CLI
- [x] 统一诊断结构、错误码、源码位置、文本/JSON 输出和退出码契约
- [x] Matlab/Python/Fortran 标量子集与 TypeScript 首个 typed 子集到 JavaScript/C++ 的端到端纵切面
- [x] JavaScript ESM/strict script 与 C++17 translation unit 输出
- [x] stdin/stdout、自动语言检测和 Fortran source-form 选择
- [x] Linux/macOS/Windows，GCC/Clang/AppleClang/MSVC CI
- [x] annotated 数字标签策略、七类 canonical 门禁复用、三平台候选完整测试/安装后 consumer/ZIP 校验、build provenance、GitHub Release 与公开资产回读验证流水线

## M1：编译器基础设施

已交付：

- [x] 多文件 SourceManager、源码所有权、UTF-8 列、CRLF、跨度、行映射和诊断片段
- [x] Matlab/Python/Fortran logical-source normalization，以及四语言表达式 lexer 和 statement token/span stream
- [x] 四语言当前子集的递归下降 statement parser；已支持语法不再使用 regex/prefix 行解析
- [x] Pratt 表达式 AST、优先级/结合性、基础错误恢复和双后端 AST pretty-print
- [x] 名称绑定、基础作用域、builtin 遮蔽与 JavaScript/C++ 保留字安全改名
- [x] 标量/容器类型、确定赋值、基础终止流、循环上下文、不可达警告
- [x] 矩形 N 维字面量/声明的 element type、shape、slice extent、rank 和静态 bounds 分析；任意 rank `RESHAPE`、直接 selector 读取/写入和双后端递归 runtime
- [x] 顶层函数依赖图、前向元数据传播、局部遮蔽排除和递归分量识别
- [x] 版本/源语言生成 banner 与 `--no-banner`
- [x] diagnostics JSON schema v1 和 CLI 契约
- [x] 前端 canonical 名称/扩展名/内容探测/解析回调统一为精确当前 `FrontendDescriptor`，旧名称缩写不进入 catalog
- [x] 后端 canonical 名称/availability/binding/validator/emitter 统一为精确当前 `BackendDescriptor`，目标只接受 `javascript`/`cpp`
- [x] 核心驱动移除源语言检测、解析和目标选择的硬编码分派
- [x] builtin 源 spelling 通过前端显式选择的共享/自有有序表解析为稳定 intrinsic ID，目标以稠密表 O(1) 查找 direct/custom binding
- [x] descriptor 冲突、检测歧义、绑定完整性、缺失绑定失败关闭和后端隔离门禁

仍需建设：

- [ ] 依据四种官方 grammar 完成 statement/parser 覆盖；版本范围 contract、公共 API/CLI gate、Python 3.8 positional-only 与 Fortran 2003 bracket-constructor gate 已落地，其余产生式/feature gate 继续逐项完成
- [x] 建立独立 HIR/MIR、强类型 ID、verifier、pass/analysis manager、确定性 dump 和生产 lowering 主链路
- [x] 语言专属 arena AST 由四个 statement parser 直接构造；窄 HIR + semantic seed、当前控制结构 MIR CFG、parser token/depth/arena 边界、Analyzer 直写 semantic side table，以及独立 name/flow/alias-effect pass 已进入生产路径
- [x] 删除 MIR 递归宽兼容树，改为与 instruction 对应的 expression/operation 稠密 arena
- [x] 删除 flat MIR 剩余源语义 payload 镜像；更细粒度 parser recovery 随官方 grammar 继续扩展
- [ ] 完整嵌套作用域、常量折叠、完整 CFG、参数敏感跨函数数据流
- [x] 跨语言一般 N 维静态 shape、声明、RESHAPE、直接 index/section 读取写入，以及 JavaScript/C++ 递归运行时；三维 Fortran/gfortran/Node.js/生成 C++ 差分已入门禁
- [x] 静态已知 shape、同一 storage root 的 element/连续/步长/N 维矩形 section overlap 与多 writable actual alias 证明
- [x] 将直接 load/store/copy/writeback 与跨函数参数 effect 统一为按 `InstructionId` 稠密的区域化 memory-access fact，并提供访问级 alias/conflict 查询
- [ ] 跨语言动态 shape 传播、一般 NDArray/typed-array 表示，以及跨 view/pointer/storage association 的完整 region 证明
- [x] revision-bound CFG memory-dependence v1：区域精化 RAW/WAR/WAW、unknown barrier、loop-carried、稠密 adjacency、verifier/dump/cache/report 与生产调用
- [ ] MemorySSA 的 memory version/phi/rename/def-use、region-aware DCE/store forwarding 与循环内存优化
- [x] source map v3、输入文件身份、生成文件身份和 LIR-origin 位置映射；banner 独立控制
- [x] 全管线 fuzz harness、拒绝/成功 corpus、确定性 mutation、libFuzzer 崩溃复现与最小化工作流

## M2：Python 3.14 前端

已交付：

- [x] 当前 `def`、赋值、分支、循环、`return` 和调用子集的 tokenized parser
- [x] 前向调用、基础直接/互递归、不可变标量默认参数和参数种类关联
- [x] positional/keyword actual、`/` positional-only 与裸 `*` keyword-only
- [x] 算术、幂、floor/true division、truthiness、`not`、operand-returning `and/or`
- [x] relational/equality 比较链与右结合条件表达式；短路、惰性且实际操作数单次求值
- [x] bool/number equality、同类递归 list/tuple equality、list/tuple 跨种类不相等、singleton/reference identity 与 string/list/tuple membership
- [x] 固定 tuple/list 解包、递归 assignment pattern、每层单 starred target 和跨函数结构元数据
- [x] `for ... in range`、正负 step、`while`、`elif`、loop-else、`break`/`continue`
- [x] logical line、括号/反斜杠续行、注释、tab 展开缩进和顶层分号 simple statements
- [x] 基础 list、矩形嵌套 list、负索引、链式索引、`len`、`sum`
- [x] `start:stop:step` 读取；普通切片 resize、extended slice 等长写入和运行时检查
- [x] `float` 的数字/布尔/字符串基础转换以及 NaN/Infinity truthiness
- [x] 基础 `int`/`float`/`bool`/`str`/`None` 标量返回 annotation 进入 Analyzer numeric side table 与 C++ 具体声明
- [x] CPython 3.14、Node.js、生成 C++ 与 oracle 差分门禁

仍需建设：

- [ ] 以 Python 3.14 grammar 建立 PEG/等价完整 parser 与旧版本语法门控
- [x] `is`/`is not`、`in`/`not in`，以及当前可表示 list/tuple equality/identity 的精确规则；`cpp` sequence identity 明确失败关闭
- [ ] dict/set/bytes、完整 string/f-string/triple-quoted literal、comprehension 和 generator expression
- [ ] lambda、named expression、完整 call/attribute/subscript/slice 表达式和 operator 集合
- [ ] 一般 iterable 解包、运行时未知长度解包，以及 attribute/subscript assignment target
- [ ] annotation、`*args`/`**kwargs`、可变默认对象、闭包、装饰器和完整函数对象语义
- [ ] 异常、上下文管理、模式匹配、async/await
- [ ] class、descriptor、dataclass、import/package 和标准库映射
- [ ] 任意精度整数、complex、精确数字转换、字符串 Unicode 细节
- [ ] 容器 alias/object identity、循环结构、异常类型与副作用顺序的完整 runtime

## M3：Matlab 2024 前端

专项成熟度分析、分期顺序、JavaScript runtime 方案、完成定义和发布门禁见
[docs/MATLAB_TO_JAVASCRIPT.md](docs/MATLAB_TO_JAVASCRIPT.md)。当前仍是实验性已验证子集，
目标版本上限不表示完整兼容 Matlab 2024。

已交付：

- [x] script logical statements、`...`、多行矩阵、行/block comment 和顶层分隔符
- [x] 当前函数/分支/循环/赋值子集的 statement lexer/parser
- [x] 字符向量与转置在 statement token 层的上下文区分
- [x] colon 计数 `for`、正负 step、`while`、`elseif`、`break`/`continue`
- [x] 一维向量、二维矩阵、1-based 行列索引和列主序线性索引
- [x] `:`, `start:stop`, `start:step:stop` 的行/列/block/线性读取与写入
- [x] 二维 shape/bounds、section conformability 和标量扩展
- [x] 文件级 local function、前向/跨函数调用、多输出绑定和转发
- [x] scalar numeric/logical/character `switch/case/otherwise`；selector 单次求值、无 fallthrough、Matlab 字符精确相等
- [x] `*`/`/`/`\`/`^` 与 `.*`/`./`/`.\`/`.^` identity；二维矩阵乘法、同 shape 或 scalar expansion 的基础逐元素算术；JavaScript/`cpp` 目标计划与 runtime
- [x] 静态已知 shape 及 local-function runtime rank/extent 的 compatible-size 隐式扩展和数组关系比较；HIR/MIR/双目标 LIR 保存并验证 shape source 与逐轴 broadcast plan
- [x] 共轭 `'`/非共轭 `.'` 的上下文解析与强类型身份；real/complex vector/rank-2 转置进入双目标独立 runtime
- [x] Matlab binary64 complex 基础语义：trailing `i`/`j` literal、可遮蔽 imaginary-unit builtin、scalar 算术/幂/一元正负、标量 `complex`/`conj`/`real`/`imag`/`abs`、complex 数组 compatible-size 逐元素运算、索引/写入/reshape、跨函数动态 numeric 分派；complex comparison/logical/reduction 以 `MPF2053` 失败关闭
- [x] 静态及运行时 extent 的逐维/线性 `end`，包括 scalar、colon、算术与 numeric selector array；保序/重复/空 numeric selector 和线性/逐维 logical selector 的列主序读写；runtime-sized extent/mask 由生成代码验证
- [x] `~`、`&`、`|` 与 `&&`、`||` 保留独立 operator identity 和 precedence；逐元素逻辑支持 scalar/static N-D/runtime operands compatible-size，短路逻辑只接受 scalar numeric/logical，`if`/`while` 对非空数组按全元素非零判断，condition-context `&`/`|` 按标量短路处理并拒绝非标量歧义
- [x] `all`/`any` 的默认首个非 singleton 维、常量 `dim`/`vecdim` 与 `'all'`；结果 rank/shape、超过 rank 的 no-op axis、`0×0`/shaped-empty identity 及未知 rank total reduction 通过强类型 `ReductionPlan` 贯穿 Analyzer、MIR 和两个目标，动态维度/字符数组以 `MPF2052` 失败关闭
- [x] vector、matrix 与一般 N 维数组的标量/range/numeric selector 自动扩容；vector 和恰好单个非 colon 轴的索引删除；线性扩容保持 vector 方向或扩展最后一维，间隙按元素默认值初始化，重复删除索引只删除一次
- [x] 稠密实数方阵/超定/欠定矩阵 `\`/`/` 求解与 safe-integer 方阵 `^`；方阵分派 exact-zero diagonal/upper/lower、full tridiagonal、exact-symmetric positive-definite 及部分主元 dense LU fallback，矩形使用 rank-aware 列主元 Householder QR，返回基本最小二乘解并对非预期秩亏稳定警告
- [x] 稠密复数二维矩阵乘法、方阵 `\`/`/` 与 safe-integer `^`；方阵按 exact Hermitian-positive-definite Cholesky 或 magnitude-pivoted dense LU 分派，右除使用共轭转置，奇异/近奇异条件稳定警告
- [x] 稠密复数超定/欠定 `\`/`/`；列主元 Householder QR 返回 pivoted basic least-squares solution，多 RHS 共享分解，秩亏时按稳定 tolerance 警告，右除按共轭转置恒等式复用同一 solver
- [x] Matlab 数组专项差分、负向语义、fuzz seed 和编译延迟/吞吐/产物大小发布门禁
- [x] Node.js、生成 C++ 与 oracle 差分框架

仍需建设：

- [ ] 完整 command/function 调用语法、character/string 数组转置和剩余字符串歧义解析
- [x] indexed deletion 与 growth；Matlab 只允许 vector 线性删除或一个非 colon 轴的整 slice 删除，静态与动态非 vector 线性删除、多个非 colon selector 和缺失维度的 shape-changing assignment 均稳定失败关闭
- [ ] 元胞数组、struct、string、table、datetime 等核心类型
- [x] exact-zero diagonal/upper/lower 与 dense fallback 的首批结构感知 solver dispatch，包含左右除和结构对应条件 warning
- [x] full-real tridiagonal 相邻行部分主元 LU、正/转置求解、条件估计，以及 exact-symmetric positive-definite Cholesky；对称非正定候选回退 dense LU
- [x] 非空静态 real rank-2 CSC：R2024 全部 `sparse` 调用形式、dense/CSC conversion/query/count、直接 canonical triplet construction、scalar expansion、duplicate accumulation/cancellation、普通/共轭 sparse transpose，以及稀疏实数方阵左右除和 storage-preserving result 子集；零 extent sparse 等待统一 shape-bearing NDArray ABI
- [ ] 一般 sparse index/reshape/logical/element-wise/multiply/power/rectangular/complex、零 extent 与动态 shape 语义，病态方阵特定解选择的精确 Matlab 对齐、非整数矩阵幂、完整 numeric class，以及一般 NDArray 值语义
- [ ] nested/anonymous function 和完整 function workspace/closure 语义
- [ ] `classdef`、properties、methods、events 与 handle/value 对象模型
- [ ] 核心函数与工具箱 API 的分层映射、许可证和版本策略
- [ ] 授权 Matlab runner 或明确批准的 Octave 兼容策略后加入源语言执行门禁

## M4：Fortran 2023 前端

已交付：

- [x] fixed/free form 自动或显式选择、continuation、注释和分号 logical statements
- [x] 当前 program、声明、IF、DO、I/O、赋值及基础 procedure 子集 parser
- [x] counted DO、DO WHILE、正负 step、ELSE IF、EXIT/CYCLE
- [x] 任意 rank 常量 extent 数组、现代/旧式构造器、RESHAPE、SIZE 和 SUM
- [x] 任意 rank 直接 section 读取/写入、默认 bound、负 stride、shape 验证和标量扩展
- [x] internal/external procedure、RETURN、RECURSIVE、RESULT 和前向/递归调用
- [x] INTENT/default intent、标量/数组/元素/section actual 引用与 copy-in/copy-out
- [x] keyword association、OPTIONAL/PRESENT、缺省调用和 optional 状态透传
- [x] integer/character/logical `SELECT CASE`、单值/范围/default、重叠验证和控制流合流
- [x] gfortran 当前可用的严格 `-std=f2018` oracle（CMake 可配置未来 `f2023`）、Node.js、生成 C++ 与声明式 oracle 差分及 source-form corpus

仍需建设：

- [ ] 预处理、`INCLUDE`、tab-form 和更完整历史 source-form
- [ ] 完整 declaration、kind/len、allocatable/pointer、derived type 和 generic interface
- [ ] module/submodule、module procedure、嵌套 procedure 和完整 interface association
- [ ] assumed-rank、assumed-size、动态 extent、pointer/target association 与跨 view 的完整 N 维 storage/alias/overlap 规则
- [ ] DO CONCURRENT、SELECT TYPE/RANK、WHERE、FORALL 与更完整 I/O
- [ ] COMMON/EQUIVALENCE/SAVE 等历史 storage 语义
- [ ] ISO_C_BINDING、外部库、BLAS/LAPACK 调用和链接适配策略

## M5：TypeScript 6 前端

当前状态：TypeScript lexical block scope、最近 binding 解析和 canonical C-style `for` 已进入共享 MIR 默认优化路径。TypeScript 使用独立 descriptor、statement token stream、编译期专属 `typescript::ast` PMR arena、verifier 与 AST→HIR visitor，不复用 Python/JavaScript parser 或其他语言 artifact；manifest 声明 1.0—6.0，但这不表示完整 TypeScript 6 grammar 已完成。

- [x] 增加 canonical `typescript` 源语言身份、`.ts`/`.mts`/`.cts` 文件探测与独立 descriptor；不把 TypeScript 作为其他 parser 的模式分支
- [x] 建立 TypeScript 全源 statement lexer、当前子集递归下降 parser、strict equality 表达式 token 和 `MPF19xx` 词法诊断
- [x] 接通 type annotation 擦除后的 number/boolean/string/array facts、默认参数、值返回、函数/分支/while/canonical-for 与 typed array 双目标 lowering
- [x] 将 explicit-only export policy 作为 semantic profile 和 side-table 事实贯通 MIR/JavaScript LIR；当前仅支持 `export function`
- [x] 建立 `ScopeModel`/`NameScopeEdges` 与双目标 lexical `ScopePlan`，支持 block-local `let`/`const`、遮蔽、最近外层赋值和 induction binding 生命周期
- [x] 对 loose equality、`var`、arrow/template、nested function、无默认值 optional parameter、非可移植比较、动态 number 索引和非布尔控制条件失败关闭
- [ ] 继续完成 TypeScript 6 官方 grammar、版本 feature gate 和产生式级 recovery/诊断；当前 parser 只是已验证子集
- [ ] 明确并实现 enum、namespace、decorator、JSX/TSX、interface/type、generic、class/object、async 和完整 module lowering 边界
- [ ] 将标准库与宿主 API 映射为 source intrinsic/外部 binding，不在 emitter 中匹配源 spelling
- [ ] JavaScript 输出扩展完整 ECMAScript truthiness/object/reference 语义；`cpp` 对动态对象模型建立明确 capability validator 与 ownership strategy
- [x] 增加 Node.js 24 source TypeScript type-stripping、生成 JavaScript、生成 C++、声明式 oracle 四路差分、typed-array case 和 TypeScript-only 自动检测/拒绝 corpus
- [ ] 接入与 manifest 版本匹配的 `tsc` 完整 type-check oracle；当前 source oracle 验证可擦除语法的运行语义，不替代完整 TypeScript checker
- [x] 更新公共 API/CLI、支持矩阵、架构/扩展/诊断文档和示例

## M6：JavaScript / C++ 后端与 runtime

代码中的 C++ 目标身份固定为 `cpp`；C++17 只表示当前生成标准，不进入枚举、组件或文件身份。

已交付：

- [x] 公共目标 API、CLI `--target cpp`、后端 registry 和 availability API
- [x] 后端 descriptor API version、canonical name、代码绑定、validator 和 emitter 回调契约；名称 alias 已删除
- [x] 数学常量/函数 direct binding 与数组/语言运行时 custom binding 分层；不再按源 builtin 字符串分派
- [x] `mpf-core`、backend-common、backend-javascript、backend-cpp 和 facade 独立 CMake 目标
- [x] javascript-only、cpp-only、core-only 构建/安装组件和外部消费者门禁
- [x] 目标无关 Analyzer 与后端独立 capability validator/emitter
- [x] C++ 函数模板、callee-first 定义、递归声明、类型/shape 驱动声明和 namespace 隔离
- [x] JavaScript Array 与 C++ 递归 `std::vector` 的索引、shape、聚合、reshape 和 section runtime
- [x] Matlab 多输出和 Fortran writable actual/optional argument 双后端 lowering
- [x] Python truthiness、and/or、条件表达式，以及 equality/ordering/identity/membership 比较链和双目标 runtime/capability 边界
- [x] Node.js 执行、生成 C++ 严格编译执行和单一差分 manifest

仍需建设：

- [x] 建立独立 JavaScript LIR/`cpp` LIR、TargetProfile、稠密 legalization、私有 semantic plan、target pass/verifier 和 opaque artifact 入口
- [x] 将两个 emitter 内 representation/ABI/type/shape/name/runtime 决策前移到目标 lowering/renderer；emitter 成为纯 serialized-chunk 序列化器
- [ ] 精确整数/浮点/complex、typed-array 布局、广播和一般 N 维数组策略
- [ ] 完整 alias/overlap、bounds policy 和源语言对象生命周期模型；当前已完成静态区域的直接/跨调用 memory-access 事实，但一般 pointer/view composition 与 MemorySSA 尚未完成
- [ ] JavaScript ESM chunking、tree shaking、稳定 name mangling 和 `.d.ts` 输出
- [ ] NPM runtime 包、semver、锁文件、SBOM、许可证审计和浏览器/Node conformance matrix
- [ ] 将当前已从 compiler renderer 拆出的 C++ runtime source catalog 进一步改为生成物侧可选独立 header/implementation、可配置 namespace 和稳定 ABI 策略；当前默认仍内联进 translation unit
- [ ] GCC/Clang/AppleClang/MSVC 生成代码 conformance 报告
- [ ] 可读/优化输出模式；编译性能 benchmark corpus 与发布回归门禁已完成

## M7：商业级交付

已交付：

- [x] clang-format、零告警 clang-tidy、ASan/UBSan、CodeQL 和依赖审查
- [x] 85% 生产代码行覆盖率门槛及 HTML/JSON 报告
- [x] declarative 差分回归、CI 结果制品和多路径不一致门禁
- [x] CMake 安装包、按后端组件导出、外部消费测试和标签发布制品

仍需建设：

- [x] 将五层 pipeline 的编译延迟、峰值 arena、产物大小、确定性、resource limit 和回归阈值纳入机器可读发布门禁
- [ ] 在 1.0 产品边界确定后设计稳定公共 C++ API/ABI、弃用周期和 LTS 分支；0.x 不提前保留适配层
- [ ] 评估并设计稳定 C17 API/ABI；当前没有可承诺的 C 接口
- [ ] 崩溃去重服务；parser/resource exhaustion 防护、fuzz replay/minimize 和性能回归门禁已完成
- [ ] 可复现构建、签名制品、provenance、SBOM 和发布密钥流程
- [ ] 完整用户手册、当前 API reference、版本化 contract 报告和架构决策记录
- [ ] release candidate、跨平台可移植性审计和 1.0 发布检查表

## 永久约束

- 所有构建、生成源码、测试结果和报告只能位于仓库根目录 `build/`。
- 废弃归档目录不得修改、构建、链接、安装或进入项目依赖图。
- C++ 目标的代码身份只能使用 `cpp`；不得新增 `cpp17`、`cxx17` 等带标准号的标识符。
- JavaScript 与 `cpp` 后端必须彼此独立；生成任一目标都不得依赖另一目标的生成物。
- 多层 IR 的固定含义是“语言 AST → HIR → MIR → JavaScript LIR/`cpp` LIR → Emitter”；MIR 不得省略或以 HIR 注解冒充，两个目标 LIR 不得合并为带 target flag 的共享结构。
- Emitter 最终只能序列化对应 LIR；源语言判断、类型/shape/storage/effect 分析、capability、binding 和 runtime dependency 选择必须在 emitter 前完成。
- 未经语义验证和自动测试覆盖的语法不得标记为“支持”；不确定时必须稳定失败并给出诊断。
- 新增语言能力原则上同时提供 JavaScript 与 C++ 行为测试；若只能支持单目标，必须在 capability validator 和支持矩阵中明确记录。
- 新增第三方/NPM 依赖前必须记录版本固定、许可证、供应链风险和跨平台影响。
