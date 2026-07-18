# MPF 架构

本文描述 0.5.7 当前源码树的实际架构。CMake 同时固定 C17/C++17 标准基线，但当前生产实现和公共 API 使用 C++17；`cpp` 是 C++ 输出目标的代码身份，C++17 是当前生成标准。项目仍处于 0.x 开发期，只维护这里描述的精确当前 contract，不提供旧 MPF 兼容层。最终职责、性能模型和后续迁移验收条件见 [商业级编译器管线方案](COMPILER_PIPELINE.md)。

## 当前状态边界

生产驱动已经切换为五层路径，旧的共享 `Program`→emitter 直通入口不存在：

- 四个 statement parser 通过 `FrontendAstBuilder<LanguageTag>` 直接产生编译期互不兼容的语言 AST artifact；递归表达式解析后立即驻留，statement body/root 只保存稠密 `AstNodeId`，错误恢复也只发布可达节点。顶层 arena 容器使用 parser session PMR resource，不存在跨语言递归 syntax tree 或 parse 后整树复制；随后显式运行 AST verifier，AST→HIR visitor 原子产出 HIR v2 窄结构与按 `HirNodeId` 稠密、绑定 revision 的 `SemanticTable` seed；
- Analyzer 和 HIR pass 只处理 HIR 与显式 side-table 输入；独立的只读 name/flow pass 分别生成 revision-bound 稠密 `NameTable` 与 `FlowTable`，负责 lexical scope/symbol/builtin、reachability/termination 和不可达诊断。`NameTable` 以 profile 驱动的 `ScopeModel` 选择 function 或 lexical-block 规则，并通过 `NameScopeEdges` 把 function、statement、body、alternative scope 绑定到 HIR owner/kind；声明只落入当前 scope，赋值/引用解析最近祖先 binding，verifier 拒绝缺失、错误 parent/kind 和非稠密 edge。Analyzer 在运行任何 pass 前验证 frontend seed，以 `ScopeId`/`SymbolId` 稠密状态消费两表并原位完善 `SemanticTable`，branch/loop 内隔离局部状态而只合并外层 binding。静态已知 shape 的 identifier、element、N 维 rectangular section 与列主序 linear section 同时规范化为 `StorageRegion` fact；动态 bound/extent 保持 unknown。HIR 节点不保存 type、shape、binding、call association、storage region 或 assignment-pattern 镜像；HIR→MIR 只从 semantic side table 读取这些 facts，并从 name table 固化 storage/function 的 `SymbolId` identity；
- MIR lowering 以 `MirExpressionId`/`MirStatementId` 建立带零号哨兵的稠密 expression/operation arena；child、body/alternative 和 roots 只保存强类型 ID，每个节点绑定 resident instruction，递归 HIR 兼容树不再驻留。MIR v17 的 revision-bound `OperationAttributeTable` 分别按 expression、statement 和 `InstructionId` 稠密保存 spelling、comparison/binding/intrinsic、Matlab broadcast/reduction/matrix-operation/solve/condition-policy/structure-policy plan、逐下标 selector/extent plan、调用策略、规范化 storage region、强类型 assignment facts 以及零到多个 `MemoryAccess`；每条访问显式保存 storage、最终 root、region 和 read/write/read-write mode，结构 `Instruction` 不重新耦合这些分析事实。if/loop/loop-else/`break`/`continue`/`SELECT CASE` 具有真实 CFG；conditional、逻辑短路和 comparison chain 进一步产生分支专属 block、`truthiness`/`compare`、typed block argument 和 edge actual。TypeScript canonical `for` 另有 preheader、condition、body、update、exit block，initializer/update 是显式 `store`，continue edge 进入 update，再由回边携带 storage actual。变量读取、声明/赋值和 section writable call 产生 `load`、`allocate`、`store`/`store_indexed`、`copy`/`writeback`，相应 read/write 区域同时进入指令属性。shape stride、storage view/lifetime/intent/optional 与 tuple/function/reference type/shape 签名均使用强类型 ID；全局 storage 以 NameTable 派生的 `SymbolId` 跨函数共享身份；
- `BinaryOperator` 是普通与 Matlab 逐元素二元运算的规范身份，`UnaryOperator` 独立保存共轭/非共轭转置；两者随语言 AST、HIR、MIR `OperationAttributeTable` 和两个目标 LIR 传递，源 spelling 只用于诊断/调试。Matlab compatible-size 运算由 `BroadcastPlan` 保存 `static_extents` 或 `runtime_operands` shape source；静态 rank 继续携带两侧/结果 shape 与逐轴 match/expand/runtime mode，未知 rank 则以显式 runtime source 和空轴清单表示，不能伪装成静态空 shape。`ReductionPlan` 为 `all`/`any` 保存 operation、默认首个非 singleton/显式维度/全维 axis policy、static-extents/runtime-operand shape source、归约轴、输入/输出 shape 和 scalar-result identity；显式 `dim`/`vecdim` 在 Analyzer 固定，未知 rank 仅允许不依赖 rank 的全维归约。`MatrixOperationPlan` 保存 multiply/left-divide/right-divide/integer-power、square/overdetermined/underdetermined solve kind、`square_continue_with_warning`/`basic_solution_with_warning` 数值条件策略、`classify_real_square` 结构策略及输入输出 shape；`IndexSelectorKind` 为每个下标分别保存 scalar/slice/numeric/logical/empty 身份，`IndexExtentSource` 则明确区分无运行时长度、当前轴长度和列主序元素总数。Matlab `[]` 在 semantic side table 中固定为 real、column-major、`0×0` sequence；目标 `ArrayLiteralPlan` 区分 direct 与 shaped-empty，防止 renderer 从空嵌套结构猜测 rank。HIR/MIR/目标 verifier 逐层重算并拒绝缺失、错误 arity/source、错误 reduction/selector/extent/solve/condition/structure policy、矛盾 shape 或错误 expression kind。Analyzer、MIR constant folding 和 target representation 不从 renderer 中反向猜测操作。两个目标分别实现二维矩阵乘法、静态稠密实数方阵 diagonal/upper/lower/pivoted-tridiagonal/symmetric-positive-definite/dense 结构感知求解、矩形 rank-aware CPQR 基本最小二乘解、safe-integer 方阵幂、静态 N 维及 local-function runtime rank/extent broadcast、数组比较、`all`/`any` 逻辑归约、vector/rank-2 转置、静态及动态 `end`、保序/重复/空 numeric selector 及线性/逐维 logical selector；runtime broadcast 和 reduction 对操作数只求值一次，验证矩形性、shape、axis 和 singleton 兼容性并保持标量结果与空归约 identity。矩形数值秩亏继续返回 pivoted basic solution 并稳定警告；方阵通过所选结构 kernel 的正/转置求解迭代估计 1-范数 `rcond`，精确奇异与近奇异分别警告后继续；未实现矩阵语义仍在 lowering/runtime 边界失败关闭；
- `NumericType` 与 coarse `ValueType`、shape 和 storage 独立：`NumericClass` 当前区分 logical、signed-integer 与 binary64，`NumericComplexity` 区分 real 与 complex；`none`、`unknown` 与非法组合同样有明确身份。语言 AST expression/assignment pattern、HIR `SemanticTable`、MIR `TypeData`、JavaScript LIR 和 `cpp` LIR 都为 scalar、element、tuple、parameter、return 和 multi-target 保存该事实，Semantic v11、MIR v17 和 LIR v23 verifier 会拒绝丢失、陈旧或 logical/integer-complex 等矛盾组合。Matlab 无类型 local-function 参数保留 unknown complexity，不能在 MIR interning 时按 coarse real/integer 擅自 canonicalize；目标 representation 因此选择动态 numeric call。JavaScript 以私有 `Symbol` tag 的 `{re, im}` object 实现 complex ABI，`cpp` 使用 `std::complex<double>`，两者分别拥有 arithmetic/broadcast/transpose runtime 和 feature pruning，生成任一目标不需要先生成或读取另一目标；
- 每个 user-call argument 是单一 region contract，保存 type、storage/root、intent、transfer、view、lifetime、writability 和 `StorageRegion`，transfer 区分 value/borrow/copy/optional-forward/omitted；copy-out 与 copy-in/out 在 call 前后形成不同 operand arity 的 temporary/writeback 指令。region 将 selector 规范化为各维零基 `first:stride:count`，同根 rectangular 区域只要任一维不相交即可证明整体 `no_alias`，列主序单 selector 使用 linearized 区域；未知、跨 kind/shape、尚未组合的一般嵌套 view 或超出有界证明能力的关系保持保守。alias relation 和 instruction/function/call effect 不内嵌结构 MIR，而由 revision-bound、可缓存的 `AliasEffectTable` v3 计算；显式 load/store/copy/writeback 从指令属性贡献区域化 read/write/allocate，跨函数 fixed point 再把 callee 参数访问按每个 actual region 实例化到 call instruction。访问级 `alias_between` 和 `memory_accesses_conflict` 是公共区域查询。独立 `MemoryDependenceTable` v1 随后在每个函数 CFG 上做 fixed point，以 `MemoryAccessSite`/`MemoryDependenceId` 建立 flow/anti/output 边；must-alias write 收敛 frontier，可证明 disjoint 的 region 不建边，unknown access 形成显式 barrier，CFG 回边传播 loop-carried provenance。该表按 `InstructionId` 稠密保存 incoming/outgoing adjacency，并与 alias/effect 一样绑定最终 MIR revision、由 `AnalysisManager` 缓存。MIR verifier 交叉检查 HIR/MIR expression fact、instruction access 与 call argument region，并检查 attribute revision/density、storage root/mode/mutability、operation ownership/可达性、lazy merge、CFG/dominance、type/shape/storage、函数签名和 call transfer/lifetime；alias/effect verifier 独立重算区域访问和跨函数 fixed point，memory-dependence verifier 再独立重算 CFG fixed point 并检查强类型 site、稠密 ID、adjacency、hazard kind、alias relation、barrier 与循环标记；
- 共享 MIR 默认优化在两个后端分叉前按 shape canonicalization、相同 edge-actual block-argument propagation、共同精确整数/布尔 constant folding + dead-pure elimination、保守 CFG cleanup 的固定顺序运行。每个 pass 都提升并同步 MIR/attribute revision、失效未保留分析、记录 instrumentation 并重新执行结构 verifier。整数折叠只覆盖 `int64` 与 ECMAScript safe-integer 的共同精确域；retired expression 使用 MIR v17 tombstone contract 保持 ID 稳定并清空 region、broadcast、matrix、selector 与 extent plan，instruction 与其 `InstructionAttributes` 则作为一个稠密单元紧凑重映射，block 同步重映射。alias/effect 与 memory dependence 只针对最终优化 revision 依次计算和验证，binding/capability 和两个后端看不到未优化或彼此不同的 MIR；
- JavaScript 与 `cpp` 通过 backend descriptor API v6 显式接收并验证 MIR 与 alias/effect facts，再分别执行 TargetProfile、逐 opcode legalization、capability、私有 semantic plan、独立 LIR/pass/verifier；两个目标 LIR v23 为 expression/statement/parameter/return/multi-target/declaration 固化 `SymbolId` 与 numeric class/complexity，名称 inventory 验证 symbol-spelling identity 与目标保留字碰撞。Program/function/statement/body/alternative `ScopePlan` 保存 lowering 后的 lexical declaration，renderer 不再按 spelling 或递归结构推导绑定；LIR 同时保存 ABI、CSR 临时资源、module/translation-unit 拓扑、source export、Matlab array-literal/broadcast/reduction shape source、matrix-operation kind/solve/condition-policy/structure-policy/shape、逐下标 selector/extent identity 与目标 expression/statement representation。强类型 `ComparisonForm` 固化 equality/ordering/identity/membership，单一 `CallArgumentPlan` 同时固化传递 ownership 与 writeback，`EvaluationForm`/call value/outcome 固化 comparison/lazy/writable-call 的 IIFE/lambda/thunk 和结果保存；按 `LirNodeId` 稠密的 `SourceSegmentPlan` 固化每个目标节点的 source/origin；
- renderer 不再根据 section AST 猜测 copy/writeback、扫描 call argument 选择 wrapper、读取源 parameter intent、递归扫描声明或动态分配临时名，也不再读取 `StatementKind`、`ExpressionKind`、`ValueType`、assignment pattern、源 index/shape、节点 location/line/origin、builtin binding、call transfer 或动态参数集合，且不接收 `TranspileOptions`、runtime requirements、函数图；两个目标 runtime source catalog 与 representation planner/verifier 均为独立编译单元。尚未结构化的一般 RAII/copy-move/runtime-call node 仍在对应 target renderer 序列化；核心只持有 opaque target artifact，两个 emitter 只调用 `serialize_chunks`。
- facade 从最终 LIR origin 构建 source map v3，并公开 dependency manifest 和包含阶段耗时/节点/峰值 arena、逐 pass 指标、MIR 变换 before/after 计数，以及 flow/anti/output/barrier/loop-carried 内存依赖统计的编译报告。

当前 memory-dependence 基础没有被冒充为 MemorySSA 或优化完成：能够表达 RAW/WAR/WAW、分支合流、循环携带、unknown barrier 和静态 region no-alias，但尚未建立 memory version/phi/def-use rename，也未启用 region-aware DCE、store forwarding 或循环内存变换。全局值编号、一般 phi/SCCP、浮点代数和跨函数优化同样未宣称完成。独立 target AST、一般 RAII/copy-move/runtime ABI node、完整四语言官方 grammar、跨语言动态 shape 数据流与一般 NDArray/typed-array 所有权、跨动态 extent/一般 view/pointer 的完整区域组合及稳定插件 ABI 仍未完成。所有边界逐项记录在 [TODO](../TODO.md)。

Matlab 索引写入由 Analyzer 唯一生成 `IndexedMutationContract`：`overwrite`、Python `resize`、Matlab `grow` 与 `erase` 均显式保存 static/runtime shape source、线性列主序身份、删除轴和输入/结果 shape。Semantic v11、MIR v17 和双目标 LIR v23 分别验证 rank、shape 变化方向与 axis；growth/deletion 在 MIR 中一律成为整个 storage root 的写入，后端只执行计划。内存依赖分析先形成当前写入所需的 RAW/WAR/WAW，再以 full-root write 裁剪被覆盖的同根历史，避免长期 mutation 序列出现二次 frontier 增长。JavaScript 使用 checked nested-array resize/axis erase，`cpp` 使用 typed nested-`std::vector` template 独立实现；Matlab null assignment 只允许 vector 线性删除或恰好一个非 colon selector 的整轴删除，静态 Analyzer 与动态 runtime 都拒绝非 vector 线性删除和多个非 colon selector。

零 extent shape 是语义事实而不是容器结构的副产物。JavaScript runtime 以不可枚举 `Symbol` descriptor 保存静态已知 shape，并在 reshape、transpose、broadcast、section 与 mutation 边界重新验证数据量和可结构观察的前缀；C++17 LIR 将 input/result shape 直接交给 length、transpose、broadcast 和 growth helper，并为 empty outer vector 补齐静态递归 rank。当前契约完整覆盖编译期已知的零 extent；跨函数动态 C++ 数组仍需统一 NDArray ABI 才能携带不可从 `std::vector` 恢复的尾随 extent。

Matlab complex 基础纵切面只授权 binary64 complex，不把 complex 冒充为 coarse `ValueType` 的新结构类别。scanner 将无空白 trailing `i`/`j` 数字识别为 imaginary literal，intrinsic catalog 将未遮蔽的 `i`/`j` 解析为 `imaginary_unit`；普通用户 binding 仍可遮蔽它们。Analyzer 对 scalar arithmetic、element-wise compatible-size array arithmetic、索引写入、reshape、`complex`/`conj`/`real`/`imag`/`abs` 和两种 transpose 生成 numeric side-table 事实；跨函数未知 complexity 使用 dynamic numeric helper。JavaScript complex division采用按分母尺度归一化的 object kernel，C++ runtime 使用相同稳定算法而不直接依赖实现定义的 `std::complex` 中间溢出行为；零指数显式返回单位元。complex comparison、logical、`all`/`any`、matrix multiply/solve/power 由 Analyzer 以 `MPF2053` 拒绝，不能落入现有 real solver policy。

Matlab 方阵除法由 Semantic v11 的 `MatrixStructurePolicy::classify_real_square` 明确选择运行时实数结构检测，MIR v17 与两个目标 LIR v23 原样传播并逐层验证；renderer 不读取 shape 或 helper 名称推导算法。JavaScript 与 `cpp` runtime 各自只求值一次系数矩阵，并按固定优先级分类：exact-zero diagonal 直接逐元素除法，upper/lower-triangular 使用前向/回代，full tridiagonal 使用与 LAPACK `DGTTRF/DGTTRS` 等价的相邻行部分主元紧凑 LU，非三对角 exact-symmetric positive-definite 使用 Cholesky，其余方阵回退到部分主元 dense LU。对称候选若 Cholesky 检测到非正定不会输出部分结果，而是进入 dense fallback。右除通过转置后的同一结构分派保持左右语义一致；每条路径使用与自身正/转置求解器一致的迭代 1-范数倒条件估计和同一 square-warning policy。当前不把容差结构识别、Hermitian、sparse 或 complex 冒充为已完成能力；R2024 full symmetric-indefinite 的 dense LU 路径属于当前 fallback contract。

标量除法的结果类型与除零行为是两个独立源语义合同。HIR v2 `Profile` 以 `Division` 固定 native/real quotient，以 `DivisionByZero` 固定 target-native、IEEE-754 或 exception；Python 选择 real quotient + exception，Matlab/TypeScript 选择 real quotient + IEEE-754，Fortran 当前保持 target-native。MIR v17 原样传播 profile，双目标 LIR v23 再分别选择目标专属 `binary_runtime_call` 或原生 ECMAScript IEEE 运算。C++17 的 IEEE 路径也必须跨越参数化 runtime helper，避免 MSVC 把惰性分支中的字面量零除在编译期拒绝；Python `/` 与 `//` 在 JavaScript 和 C++17 runtime 中先检查零分母并给出稳定错误。renderer 只序列化已验证的 helper token，不读取源语言、除零策略或 helper 名称；旧的 C++ direct-scalar-division LIR form 已移除。

逻辑求值同样不由 renderer 猜测。Semantic v11 的 `LogicalEvaluation` 为表达式指定 `eager_elementwise`、`short_circuit_boolean` 或 `short_circuit_operand`；MIR v17 据此决定直接求值或显式 CFG/typed merge，并在 Matlab boolean-short-circuit 路径把 operand 规范成 scalar logical，而 Python operand-short-circuit 仍合流原值。两个目标 LIR v23 原样保存策略，再分别选择 compatible-size logical runtime、scalar short-circuit 或 unary logical helper。Matlab statement condition 另由 profile 固定为“非空且所有元素非零”，`&`/`|` 只有在 condition logical-composition 上下文才采用 scalar 短路；数组必须通过 `all`/`any` 显式归约。Semantic/MIR/LIR dump 和逐层 verifier 都重算 operator/context 关系，防止符号 identity、eager/lazy 策略或 truthiness 在后端分叉后漂移。

`all`/`any` 也不是普通 helper 名称映射。Analyzer 生成的 `ReductionPlan` 区分默认首个非 singleton 维、常量 `dim`、常量 `vecdim` 与 `'all'`，并在静态 shape 上直接计算输出 extent；高于 rank 的维度是 no-op，`0×0` 默认归约分别使用 `all=true`、`any=false` identity。Semantic v11、MIR v17 与两个目标 LIR v23 独立重算 intrinsic、axis 和 shape 关系，优化 tombstone 必须清空 plan。JavaScript 通过带 shape descriptor 的列主序 kernel 保留不可结构恢复的零 extent，`cpp` 通过静态 shape 模板参数和递归 `std::vector` kernel 独立生成相同结果；未知 rank 只为 `'all'` 使用 runtime total reduction。当前字符数组、动态 `dim`/`vecdim` 与未知 rank 的维度保持型归约以 `MPF2052` 失败关闭，等待统一 NDArray/numeric-class 模型，不由 emitter 猜测。

## 设计原则

- 前端彼此隔离，只负责源语言语法和语义；禁止在解析器中直接拼接目标语言代码。
- HIR 表达规范化源语义，MIR 表达目标无关执行语义；后端不得从源语言身份猜测行为。
- 不支持或无法保持语义的结构必须产生诊断，禁止静默降级。
- 公共 API 只位于 `include/mpf/`；`mpf/mpf.hpp` 是完整入口，细粒度头文件可按需包含。配置期生成的 `mpf/version.hpp` 来自 `cmake/templates/version.hpp.in`，模板不混入可安装头文件树。0.x 期间公共与内部接口均以当前源码为唯一 contract，不形成跨版本兼容承诺。
- 默认输出 JavaScript ESM，也可选择 C++17；两种输出都应确定、格式稳定并适合代码审查。

## 编译流程

```text
source + options
      │
      ▼
Frontend Registry
name / alias / extension / scored content probe
      │
      ▼
multi-file SourceManager + stable file table
      │
      ▼
FrontendDescriptor.parse → language AST artifact
      │
      ▼
language-specific logical-source normalization
      ├─► Python bracket/backslash/indent/comment rules
      ├─► Matlab ellipsis/matrix/comment/separator rules
      ├─► Fortran fixed/free source-form rules
      └─► TypeScript full-source comment/token boundaries
      │
      ▼
statement syntax layer
      ├─► Python statement lexer ─► recursive parser
      ├─► Matlab statement lexer ─► recursive parser
      ├─► Fortran statement lexer ─► recursive parser
      └─► TypeScript statement lexer ─► recursive parser
                                  └────────► diagnostics
      │
      ▼
FrontendDescriptor.verify → FrontendDescriptor.lower
      │
      ▼
HIR + frontend SemanticTable seed verifier → independent NameTable/FlowTable → direct SemanticTable analysis
source intrinsic spelling ─► stable IntrinsicId
      │
      ▼
HIR→MIR → MIR verifier → shared MIR optimization + per-pass verifier
                         → optimized-revision alias/effect analysis + verifier
                         → CFG memory-dependence analysis + verifier
      │
      ▼
Backend Registry ─► BackendDescriptor
      │
      ├─► JavaScript profile/legalization/capability → semantic plan → JS LIR/pass/verifier → emitter
      │
      └─► cpp profile/legalization/capability ───────► semantic plan → cpp LIR/pass/verifier → emitter
```

上图是当前实际生产路径；最终职责仍固定为：

```text
language AST
  → HIR：消除语言表面差异
  → MIR：类型化 CFG、值、shape、storage；共享优化后重算独立 alias/effect 与 memory-dependence 分析表
  → JavaScript LIR / cpp LIR：目标专属 capability、binding 与 lowering
  → emitter：确定性序列化
```

五层已有不同强类型表示和 verifier。生产 AST 不包含共享 syntax tree，最终 emitter 不包含 lowering；name、flow 和 Analyzer 语义事实分别只有 `NameTable`、`FlowTable`、`SemanticTable` 一个 owner，三者都以 HIR revision 拒绝 stale/missing facts。HIR 已删除宽语义字段及共享 syntax lowering 旁路；MIR 已删除递归兼容 ownership和宽语义 payload，结构节点、revision-bound `OperationAttributeTable` 与驻留 type/shape/storage 表各自只有一个职责。详细 contract 和禁止依赖见 [COMPILER_PIPELINE.md](COMPILER_PIPELINE.md)。

前端和后端 descriptor 只保存大小写不敏感的 canonical name；源语言为 `matlab`、`python`、`fortran`、`typescript`，目标为 `javascript`、`cpp`。文件扩展名仍用于自动探测，但 `py/f90/ts/js/c++` 不再作为 catalog 名称。公共解析入口返回 `std::optional`，未知名称不能静默回退到 automatic 或 JavaScript。descriptor 中的 API/schema version 必须与当前 producer、registry、verifier 和 consumer 精确一致，不提供旧 descriptor adapter。

CMake 安装包使用 `ExactVersion`，只导出标准的 `mpf_<component>_FOUND` component 状态；旧式大写 availability 变量和旧版本 consumer 请求已删除。诊断对象在构造时即拥有完整 range，renderer 不为缺失结束位置的历史结构合成数据。架构门禁会阻止版本范围、兼容名称字段、歧义解析 API、旧包变量和诊断兼容分支恢复。

前端 manifest 声明 minimum/maximum **源语言**版本、feature bitset、resource contract、AST schema、确定性和 reentrancy，并通过工厂创建独立 parser session；后端 manifest 声明目标标准、artifact/configuration schema、runtime license/supply-chain、TargetProfile 和 legalization factory。源语言旧标准支持与旧 MPF 兼容是两个不同问题：前者仍是产品能力，后者在 0.x 不提供。catalog 保持静态只读、无全局构造器自注册，conformance harness 重复 lowering/dump/emit 并比较确定性。当前 descriptor contract 用于编译期内置组件，不是稳定动态插件 ABI；接入步骤见 [扩展指南](EXTENDING.md)。

## 构建与链接边界

```text
mpf facade
├── mpf-core
├── mpf-backend-javascript ──► mpf-backend-common ──► mpf-core
└── mpf-backend-cpp        ──► mpf-backend-common ──► mpf-core
```

- `mpf-core`：源码、lexer、前端 descriptor/registry、语言 AST artifact、HIR/MIR、pass/analysis、intrinsic catalog、代码绑定验证和目标无关 Analyzer；物理 target 合并以控制构建时间，静态 architecture test 约束 include 方向。
- `mpf-backend-common`：目标 profile/legalization 公共机制和确定性名称计划；core-only 构建不会编译它。
- `mpf-backend-javascript`：JavaScript 稠密代码绑定、capability、semantic plan、LIR/pass/verifier 和 emitter。
- `mpf-backend-cpp`：`cpp` 稠密代码绑定、capability、semantic plan、LIR/pass/verifier 和 emitter。
- `mpf` facade：公共转译 API 与可选 backend registry；不直接包含具体 emitter 头文件。

当前这些 C++ target 均显式构建为静态库，避免 `BUILD_SHARED_LIBS` 在未定义导出宏、可见性、稳定 ABI 和跨模块 allocator contract 时意外产出不可支持的动态库。稳定共享库将在 C ABI/符号导出/所有权/版本协商边界完成后作为独立产品能力交付，不把内部 0.x C++ descriptor 当作动态 ABI。

目标源码按目录而不是文件名前缀区分：`src/backends/javascript/` 与
`src/backends/cpp/` 各自包含 `backend`、`bindings`、`lir`、`lowering`、`renderer`、
`runtime`、`validator` 和纯 `emitter`；跨目标的 artifact、pipeline、registry、identifier
与 target-LIR 工具集中在 `src/backends/common/`。`src/backends/` 根目录不放实现文件。
架构门禁拒绝恢复平铺文件，也拒绝目标目录 include 另一目标。

隔离测试会为 javascript-only、cpp-only 和 core-only 分别创建全新构建树，检查 compilation database 中不存在禁用后端源码，执行可用/不可用目标诊断，安装对应组件，再由外部 CMake 消费者验证导出依赖。

生成某一目标只读取公共 IR 和该目标组件；任何后端都不读取另一后端的源码输出。废弃归档内容不属于依赖图，也不得由 CMake 或安装包引用。

## 目录边界

```text
include/mpf/                 public installed C++ API
cmake/templates/             configure-time generated-header templates
src/lexer/                   source-language-neutral token/scanner primitives
src/frontends/common/        frontend descriptor, registry and arena support
src/frontends/<language>/    language-owned normalization, lexing, parsing and frontend
src/backends/common/         cross-target backend infrastructure
src/backends/<target>/       target-owned lowering, LIR, runtime, rendering and emission
```

- `core`：公共入口、诊断以及编译会话生命周期。
- `source`：SourceManager 拥有多份源码、稳定文件表、UTF-8 位置、行表和跨度。
- `lexer`：只保留公共 token/scanner、语言独立词法规则和共享 `BasicStatementToken` 载体；表达式与 statement lexer 都由 `frontends/<language>/` 所有，不存在平行的 `src/lexers/`。通用表达式 parser 接收语言目录提供的 lexer 回调和 token 结果，公共 lexer 层不以 `switch(SourceLanguage)` 枚举具体语言。
- `compiler`：递归 assignment pattern/value metadata、Pratt 表达式 parser、轻量 statement identity、通用 HIR 函数依赖图、稳定 intrinsic ID 和代码绑定验证；不定义跨语言 syntax `Program`/`Statement`。
- `ir`：强类型 ID、semantic profile、HIR、MIR lowering、独立 MIR opcode/verifier、共享 MIR optimization pipeline、revision-bound alias/effect 与 CFG memory-dependence 分析、pass/analysis manager 与确定性 dump。
- `semantic`：目标无关的作用域、名称绑定、builtin 遮蔽、确定赋值、循环上下文、不可达代码、表达式分支类型、标量/元素类型、矩形 shape、动态 extent、slice 长度、section conformability、逐维静态越界和 rank。
- `frontends`：`common/` 拥有 descriptor/registry 和只共享 arena 生命周期机制的 `FrontendAstBuilder`；每个语言目录独占 logical-source normalizer、表达式/statement lexer、递归下降 parser 与 frontend factory。parser 消费 statement token/span，把表达式跨度交给 Pratt parser 并立即驻留到本语言 arena。当前迁移覆盖已支持子集，完整官方 grammar 仍按各语言里程碑扩展。
- `backends`：`common/` 拥有 descriptor、registry、revision-checked artifact/pipeline 和跨目标名称/LIR 工具；每个目标目录独占 intrinsic binding、capability/semantic plan/LIR/pass/verifier、resource/layout/representation planner、renderer、runtime source catalog 与纯 serialized-chunk emitter。Python loop-else 使用每层独立完成标志；数组访问的 nested/matrix/section 形式由目标 `ExpressionPlan` 固化，并消费 shape/bounds/base/negative-index/column-major 元数据。生成 C++ 放入 `TranslationUnitPlan` 固化的 `mpf_generated` namespace，runtime 放入独立 `mpf_runtime` namespace。
- `cli`：文件和参数 I/O，不包含编译语义。

## 正确性策略

每一项语言能力至少包含：成功样例、边界样例、拒绝样例，以及两个目标后端的行为测试。可执行成功样例进入单一 corpus manifest；JavaScript 使用 Node.js 做语法/执行验证，C++17 生成物使用顶层相同 compiler/generator 严格编译执行，源 runner 可用时直接参与同一结果比较。诊断码、JSON schema 与 CLI 退出状态是工具集成契约，删除或重定义需记录在变更日志；详见 [测试文档](TESTING.md) 与 [诊断文档](DIAGNOSTICS.md)。

## 依赖策略

核心库当前不依赖第三方组件。未来引入解析器生成器或 NPM/runtime 组件前，需记录版本、许可证、供应链固定方式和跨平台影响。标准数值函数分别映射到 ECMAScript `Math` 与 C++ `<cmath>`；仅当目标语言缺少等价语义时才引入小型、可审计的 runtime。
