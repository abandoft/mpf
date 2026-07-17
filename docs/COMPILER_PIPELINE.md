# MPF 商业级编译器管线方案

本文是 MPF 前端、公共中间表示、分析/优化基础设施和后端的权威架构规范，也是重构验收依据。若其他文档与本文的层级职责冲突，以本文和 [TODO](../TODO.md) 的逐项状态为准。

> 当前状态：生产路径固定为语言专属 PMR arena AST→窄 HIR + revision-bound semantic/name/flow side table→MIR→共享优化→区域化 alias/effect→CFG memory-dependence→目标 semantic IR/rendered LIR→纯 emitter，并实际执行逐层 verifier、pass/analysis、TargetProfile/legalization、opaque artifact、确定性 dump/golden、资源 contract、conformance、source map、编译报告、fuzz 与性能门禁。0.x 只维护这一份精确当前 contract：descriptor 只接受 canonical 名称，公共名称解析显式返回 `std::optional`，CMake package 只接受精确当前版本，不保留旧 MPF adapter、alias、overload、包变量或 schema reader。后续迁移集中在完整独立 target AST、一般 RAII/copy-move/runtime ABI node、完整四语言官方 grammar、动态 rank/extent 广播、跨一般 view/pointer 的区域组合、MemorySSA/region-aware optimization 和插件 ABI；不能把当前 TypeScript 子集等同于完整 TypeScript 6 兼容。

## 目标与永久约束

目标是让增加源语言、增加输出语言、增加代码绑定或增加优化 pass 都只触及明确边界，同时保持可预测的编译时间、内存使用、诊断和生成结果。

- 源语言前端不得生成目标语言字符串，也不得依赖任一后端组件。
- JavaScript 与 `cpp` 后端只共同依赖 MIR contract，不共享 LIR、emitter 或生成产物。
- `cpp` 是 C++ 输出目标唯一的代码身份；C++17 只是当前生成标准，不进入枚举、目录、组件或文件命名。
- 不可保持的语义必须在对应 verifier、capability pass 或 lowering 阶段失败关闭，不能让 emitter 猜测。
- 废弃归档内容不得被修改，也不得进入 include、链接、安装、测试或生成依赖图。
- 内置 registry 必须只读、确定且无全局构造器自注册；未来动态插件必须另设稳定 C ABI，不直接暴露内部 C++ 对象布局。

## 权威五层模型

```text
SourceManager
  │
  ├─► Python AST
  ├─► Matlab AST
  ├─► Fortran AST
  ├─► TypeScript AST
  └─► future language AST
          │  frontend-owned AstToHir lowering
          ▼
        HIR + dense SemanticTable seed
          │  seed verification + normalization + semantic analysis
          ▼
        MIR
          │  shared verified optimization pipeline
          ▼
        optimized MIR + recomputed alias/effect
          │
          ├─► JavaScript backend pipeline
          │     capability → legalization → semantic IR → AST/LIR → verifier → printer
          │
          └─► cpp backend pipeline
                capability → legalization → semantic IR → AST/LIR → verifier → printer
```

五层必须是不同的强类型数据结构，不能用 typedef、布尔 stage 标记或同一个宽结构体冒充不同层。层间只通过显式 lowering result 移交所有权；每次 lowering 都返回 IR、诊断和可选统计信息。

## 共同基础：身份、所有权与 side table

所有 IR 使用强类型 ID，禁止长期保存跨 arena 的裸指针：

```text
FileId / SourceSpan
AstNodeId
HirNodeId / ScopeId / SymbolId
MirFunctionId / BlockId / ValueId / TypeId / ShapeId / StorageId
LirNodeId / RuntimeSymbolId
```

- `0` 或专用 sentinel 表示无效 ID；ID 在所属 compilation session 内稳定。
- 节点放入按阶段分离的 arena/monotonic resource；常用查询通过 ID 索引稠密 `vector` side table。
- 类型、字符串、shape 和符号名使用 session 级 interning，避免节点重复拥有大对象。
- 分析结果不反复写回语法节点；dominance、liveness、alias、effect 等结果由带 revision 的 side table 持有。
- AST→HIR lowering 同批建立结构节点及其 `SemanticTable` slot；name/flow pass 只读 HIR 并生成 `NameTable`/`FlowTable`，Analyzer 验证并直接完善 frontend seed。任何规范化 pass 改变 HIR 结构时必须提升 revision，并同时重排 semantic table；禁止仅重排 HIR ID。
- `NameTable` 的 scope graph 由 semantic profile 选择 function 或 lexical-block model；function/statement/body/alternative edge 以 HIR owner 和 `NameScopeKind` 验证，声明属于当前 scope，引用与赋值按 parent 链解析最近 binding。Analyzer 进入/离开 scope 时只保留可见 symbol state，目标 LIR 再按同一 `SymbolId` identity 建立自己的 lexical declaration plan。
- lowering 消费上层 IR 或借用只读视图；禁止无必要地深拷贝整棵树。
- 公共 IR 默认只在单个 session 内可变；发布给下一阶段后视为只读。调试构建可用 revision/frozen 标记检测越界修改。

## 第一层：语言 AST

每种源语言拥有自己的 AST 类型和目录，例如 `python::ast`、`matlab::ast`、`fortran::ast`、`typescript::ast`。共享的只有 source/span、诊断、arena 和少量 token 基础设施。

AST 负责：

- 准确表达该语言的语法结构、版本门控和错误恢复结果；
- 保存 `SourceSpan`，必要时保存 token/trivia 引用，以支持精准诊断、source map 和格式工具；
- 区分源语言表面结构，例如 Python comparison chain、Matlab 多输出签名、Fortran source form 与 declaration attribute。

AST 禁止包含：

- JavaScript/`cpp` 名称、runtime helper 或代码片段；
- 已选择的目标类型、目标 include/import 或目标能力判断；
- 通过 `matlab_*`、`python_*` 等布尔字段塞入共享 IR 的长期捷径。

每个前端包应包含：

```text
descriptor
source normalizer
lexer
parser
AST model + AST verifier
AST → HIR + semantic seed lowering
language-version/capability manifest
frontend conformance tests
```

接入新语言时只允许新增自己的 AST 包和 AST→HIR + semantic-seed lowering。若窄 HIR 结构或 `SemanticTable` 无法无损表达语义，应先扩展对应 contract 和所有 verifier，不得绕过它们直接修改 MIR 或后端。

## 第二层：HIR

HIR 是结构化、目标无关的高级表示。它消除语法差异，保留便于诊断和分析的函数、分支、循环、调用、section 等结构；易变或较宽的类型、形状、绑定、调用关联和 assignment-pattern facts 由同 revision 的 `SemanticTable` 独占。

HIR 负责：

- 规范化函数、参数/结果名称、默认参数节点、调用与多目标赋值的结构；
- 把不同表面语法转换为显式的统一操作，例如 range loop、slice/section、structured selection；
- 通过同批 semantic seed 保存已解析的 `IntrinsicId`、assignment pattern 和源语义事实，不保存源 builtin 到目标符号的目标绑定；
- 在 program semantic profile 与 side table 中描述 truthiness、division、index、layout、comparison 等规则，不靠后端按 `SourceLanguage` 猜测；
- 保存完整 source origin 链，允许一个 HIR 节点映射多个 AST span 或反向映射。

HIR 禁止包含：

- JavaScript IIFE、C++ lambda、include/import、NPM package 名称或目标 runtime symbol；
- CFG block、phi、liveness 等 MIR 专属结构；
- emitter 才能解释的字符串操作符协议。

HIR verifier 检查 ID 唯一性、所有权、节点 arity、presence flag 和结构不变量；semantic verifier 独立检查 revision、稠密 ID 覆盖、origin、type/shape/call/parameter/result/assignment facts arity。两者必须在 Analyzer 前同时通过。

## 第三层：MIR

MIR 是真正独立的一层，不是带更多注解的 HIR。它以函数和控制流图为单位，显式表达类型化值、执行顺序、shape 与 storage operation；alias 和副作用由与 MIR revision 绑定的独立分析表表达。MIR 与其已验证分析事实共同构成公共优化和所有后端的输入。

### MIR 数据模型

```text
MirModule
├── expression inventory: dense arena<MirExpressionId, resident InstructionId>
├── operation inventory: dense arena<MirStatementId, resident InstructionId>
├── revision-bound dense OperationAttributeTable
├── operation roots/body/alternative: strong-ID ownership edges
└── MirFunction
    ├── signature + calling convention semantics
    ├── blocks: dense arena<BlockId, BasicBlock>
    ├── SSA values: ValueId
    ├── types/shapes/storage: TypeId / ShapeId / StorageId
    └── source origins

BasicBlock
├── block arguments / phi equivalents
├── ordered instructions
└── exactly one terminator
```

arena 只通过强类型 ID 连接，目标后端必须 O(1) lookup 后构造自己的 LIR，不能恢复递归 MIR ownership。每个 expression/operation 节点都与真实 instruction 核对 origin/opcode/operand/SSA/type/shape/storage；roots 与 body edge 必须形成无重复、无环且覆盖全部 operation 的 ownership graph。`OperationAttributeTable` 与 MIR revision、expression/statement inventory 一一对应，只保存无法由驻留 `TypeId`/`ShapeId`/`StorageId`/`Function` 推导的 operation 属性；stale、缺行、错误 origin、无效强类型 ID 或 arity 分歧必须失败关闭。conditional、短路逻辑与 comparison chain 在 MIR 中生成 condition terminator、分支专属求值块和 typed block-argument merge；canonical C-style `for` 使用 preheader/condition/body/update/exit block，initializer/update 为显式 store，continue target 固定为 update block，break target 固定为 exit。目标 `EvaluationForm` 或 loop form 只选择 JavaScript IIFE/`for` 或 `cpp` lambda/`for` 等目标表示，不再独占 lazy/loop 语义。

核心 instruction family 至少覆盖：

- pure scalar/container operations；
- call/intrinsic call 与多结果值；
- load/store、element/section read/write、allocate、copy、view；
- shape/rank/stride/layout 转换与 bounds policy；
- branch、switch、loop backedge、return、unreachable；
- 显式 conversion、truthiness、equality 和源语言定义的数值行为；
- 可扩展的 effect 与可能失败操作。

### 类型、shape 与 storage

- `TypeId` 描述标量、tuple、sequence、array、function/reference 等逻辑类型。
- `ShapeId` 独立描述 rank、静态/动态 extent、stride、layout 和 section view。
- `StorageId` 表示可能共享或重叠的存储区域；view、optional parameter、copy-in/copy-out、writable actual 和 lifetime 必须显式关联。
- `StorageRegion` 是不修改 HIR 结构节点的稠密 semantic/operation side-table fact：`rectangular` 保存 root shape 及每维零基 `first/stride/count`，`linearized` 保存列主序单 selector 的线性 progression，`unknown` 明确表示不能作精确结论。
- MIR v9 不把区域或 effect 塞回 `Instruction`；revision-bound `OperationAttributeTable` 按 `InstructionId` 稠密保存 `InstructionAttributes`，每条指令可持有零到多个 `MemoryAccess {storage, root, region, mode}`。expression 属性另以强类型 `MatrixOperationPlan` 保存 Matlab matrix kind、square/overdetermined/underdetermined solve kind 和输入/输出 shape ID，并以逐下标 `IndexSelectorKind` 保存 scalar/slice/numeric/logical/empty identity。直接 load/index/slice 产生 read，store/store-indexed/loop-variable/writeback 产生 write，copy-in/out 只在 copy-in 阶段读取 original region，copy-out 不伪造读取；调用自身的跨函数读写在 alias/effect fixed point 中按 actual region 实例化。
- alias 结果不内嵌 `StorageData`，而在稀疏 side table 中使用 `no_alias`、`may_alias`、`must_alias` 等保守格；未知时不能假设不重叠。
- mutable aggregate 不强行伪装为纯 SSA 值；通过 value SSA 加显式 memory/storage effect 表达，后续可演进 memory SSA。

每个 user-function call argument 使用一个聚合 contract，不使用容易失配的 type/storage/omitted 并行数组：

```text
CallArgument
├── logical TypeId
├── actual StorageId + root StorageId
├── ParameterIntent + writability
├── view kind + actual lifetime
├── normalized StorageRegion
└── transfer: value | read-only borrow | mutable OUT/INOUT borrow
              | copy-out | copy-in/out | optional forward | omitted
```

section writable actual 的 copy transfer 在 MIR 中确定，并在 call 前后生成带 expression-lifetime temporary 的 `copy`/`writeback`；copy-out 没有输入值，copy-in/out 读取原 section，两者都以 transfer mode、type/shape、目标 storage 和 region 接受 verifier 与 alias/effect 检查。目标 lowering 只能选择 box、reference、temporary 或 runtime ABI 等表示，不能重新根据 AST 形状判断是否 copy-out 或重算 selector overlap。

静态 region 关系按 N 维笛卡尔积计算：root shape/kind 不同或任一事实 unknown 时返回 unknown；相同非空事实返回 identical；任一空集或任一维 progression 被证明不相交时整体 disjoint；其余返回 overlaps。单维证明先做范围和单点检查，再用 stride 的 gcd 同余快速排除交错序列，并只在较小 progression 不超过固定上限时枚举；大规模同余兼容但无法在常数资源内证明的关系保守视为 overlap。Fortran 多个 writable actual 只有在当前 region 与所有既有同根 region 都为 disjoint 时放行，动态 bound/extent 不会被猜测为安全。

### 副作用模型

独立分析表按 `InstructionId` 稠密保存 local/transitive `EffectSet`、storage read/write set 和规范化 memory-access 列表，按 `MirFunctionId` 保存参数读写/escape 与 unknown-memory fixed point，并按 call-site 保存实参实例化结果。访问级 `alias_between` 先检查最终 root，再用 region relation 将同根访问精化为 no/must/may alias；`memory_accesses_conflict` 只在至少一侧写入且不能证明 `no_alias` 时返回 true。`EffectSet` 至少能区分：

```text
pure
read(StorageId / unknown)
write(StorageId / unknown)
allocate
io
may_throw / trap
control
external_unknown
```

函数摘要只把 global/unknown memory 与非 memory effect 直接传播到调用点；参数读写/逃逸通过 call-site actual storage 实例化，纯函数局部 storage 访问不得伪装成调用者可见写入。每个 call effect fact 同时保存 argument transfer、normalized region 和成对 overlap relation；同根 storage 先按 region 将基础关系精化为 `no_alias`/`must_alias`，未决关系仍保持 `may_alias`，多个 writable actual 若不是 `no_alias` 则失败关闭。全局 storage 由 name analysis 的 `SymbolId` 统一身份，参数 storage 与潜在实参之间保持保守 `may_alias`。

公共优化只有在 effect、alias 和 evaluation-order 证明允许时才能重排或删除指令。外部未知调用默认读写未知存储并可能失败，除非 binding manifest 提供更强保证。

### CFG 内存依赖

`MemoryDependenceTable` 是 alias/effect 之后的独立只读分析，不是 `Instruction` 字段，也不是任一后端的私有数据。它必须满足以下 contract：

- 表与最终优化后的 `Program::revision` 绑定；按 `InstructionId` 稠密保存 incoming/outgoing `MemoryDependenceId`，边表使用零号哨兵和连续强类型 ID；`AnalysisManager` 的 entry 存储必须保证新增其他分析时已返回引用仍稳定，只能在显式 invalidation 时失效；
- 每个 `MemoryAccessSite` 指向 alias/effect instruction 中确定 ordinal 的区域访问，或指向该指令唯一合并的 unknown-memory site，并携带 read/write/read-write mode；
- CFG fixed point 只为不能证明 `no_alias` 的访问对建立依赖：prior write→current read 为 flow/RAW，prior read→current write 为 anti/WAR，prior write→current write 为 output/WAW；read-write 可同时形成三类边；
- must-alias write 在 transfer 后裁剪已覆盖 frontier，避免无界历史累积；may-alias 不得被乐观 kill；同一指令内的并列访问不互相制造顺序，但回边带回的同一 site 可以形成 loop-carried 自依赖；
- 使用按稳定 block/successor 顺序运行的非递归 DFS cycle cut 标记反馈边；有向 CFG 的每个环至少跨越一个 gray-edge feedback edge，因而自然循环与不可归约环都能保守传播 loop-carried provenance，同时避免全图 reachability/dominator 的平方空间。unknown access 与任意潜在冲突形成 `may_alias` barrier；
- verifier 必须先要求当前 alias/effect 表，再独立重算 expected fixed point，拒绝 stale/incomplete 表、损坏哨兵、非稠密 ID、错误 site ordinal/mode、无别名边、adjacency 不一致以及错误 barrier/loop/hazard facts；
- dump 与公共 `CompilationReport` 分别稳定公开完整依赖边，以及 flow/anti/output/barrier/loop-carried/涉及指令/总数。分析复杂度必须由版本化性能场景约束，不允许用放宽超时掩盖退化。

这一级是 MemorySSA 的输入，而不是 MemorySSA 本身。memory version、memory phi、rename/def-use 以及消费它们的 region-aware DCE/store forwarding 只有在各自 verifier、差分、fuzz 和性能门禁完成后才能标记为已交付。

### MIR pass 与 verifier

HIR→MIR lowering 必须显式生成 CFG 和 evaluation order。结构 verifier 与 alias/effect verifier 分责，合计至少检查：

- block/edge、entry、terminator 和可达性结构；
- definition dominates use，block argument/前驱参数数量和类型一致；
- `ValueId`/`TypeId`/`ShapeId`/`StorageId` 引用有效；
- operation attribute revision、密度、origin、tuple/pattern/target arity 与强类型引用有效；
- lazy truthiness/compare、merge edge、load/allocate/store/store-indexed 和 copy/writeback 的 operand、type、shape、transfer、writability 与 storage 规则；
- effect 与 instruction kind 的最低约束；
- return/call signature、多结果和异常/失败边一致性。

默认公共管线按固定顺序运行，并在每个变换后提升 `Program::revision`、同步 `OperationAttributeTable::mir_revision`、失效未声明保留的分析、记录耗时和执行完整 MIR verifier；instruction compaction 必须与稠密 `InstructionAttributes` 同步重映射：

1. `mir-shape-canonicalization` 重新计算静态 row/column-major canonical stride，按 rank/layout/extent/stride 去重 shape，并一次性重写所有强类型 `ShapeId` 引用；dynamic-rank 的运行时 stride 不被臆测。
2. `mir-copy-propagation` 只删除带 storage 身份、且每条 incoming edge 的 actual 完全相同的 block argument；同时按同一 ordinal 删除所有前驱 actual，其他 phi-equivalent 合并不做猜测。
3. `mir-constant-folding-dce` 只折叠同时落在 `int64` 与 ECMAScript safe-integer 共同精确域的 checked 加减乘/正负号、布尔非和可证明整数/布尔比较；溢出、目标共同精度外整数、除法、实数、identity/membership 和 lazy CFG 保持不动。折叠后仅回收 opcode contract 已证明纯的 literal/unary/binary 子树，保留稳定 expression tombstone 并紧凑重映射 resident instruction。
4. `mir-cfg-cleanup` 只删除非 entry、无参数、无 instruction 的单目标 forwarding block，以及无前驱的空 unreachable block；随后紧凑重映射 `BlockId`、successor 和函数 block inventory。

默认管线先验证 lowering 输出，再执行上述保守变换；alias/effect 和 CFG memory-dependence 分析只在最终优化 revision 上依次计算并验证，capability、binding 和两个目标 lowering 因而消费完全相同的 MIR。后端不得复制一份目标专属常量折叠或要求先生成另一目标。涉及 effect 重排、浮点代数、跨调用传播、一般 phi、MemorySSA、region-aware DCE/store forwarding 的优化仍未启用，必须先补证明、负向 verifier、差分、fuzz 和性能基准。

## 第四层：分层的目标后端

全局管线把目标部分概括为 LIR，但每个商业级后端内部也必须分层。JavaScript 与 `cpp` 后端分别拥有自己的 capability、legalization、target semantic IR、target AST/LIR、target pass/verifier 和 printer；它们可以复用只读 MIR 工具和 Backend SDK，但不能 include 对方的头文件。

```text
read-only MIR
  → TargetProfile + Capability
  → Legalization
  → Target Semantic IR
  → Representation / ABI lowering
  → Target AST / LIR
  → Target canonicalization + Verifier
  → Emitter / Printer
```

### Capability 与 TargetProfile

`TargetProfile` 是只读配置，描述目标语言/标准版本、宿主、module mode、数值和容器表示、runtime policy 以及资源限制。Capability pass 只判断给定 MIR feature 是否能被目标保持，并产生稳定诊断或 `LegalizationPlan`；它不能生成代码字符串。

### Legalization

Legalization 通过按 `MirOpcode`/semantic operation 注册的稠密规则表，把目标不能直接表达的 MIR 操作改写为目标支持的操作、显式控制流或 runtime call。所有 MIR opcode 必须明确选择 direct、rewrite、runtime 或 unavailable，缺项失败关闭。

Legalization 必须保持 MIR 已确定的求值顺序、effect、storage/alias 和失败行为。它可以按目标拆分多结果、materialize 临时值或选择 runtime，但不能重新解释源语言 AST。

### Target Semantic IR

Target Semantic IR 保存已经合法化但仍不绑定最终语法排版的目标语义，包括：

- 具体目标类型与数据表示；
- calling convention、ownership/reference/value policy；
- 已解析的 `RuntimeSymbolId`、runtime feature 和 dependency manifest；
- module/chunk/translation-unit 规划；
- 名称需求、临时值生命周期和 source origin。

Target Semantic IR 是各后端私有的强类型。不能使用一个共享结构体加 `TargetLanguage` 分支代替 JavaScript/`cpp` 两种类型。

### Representation/ABI lowering 与 Target AST/LIR

这一阶段把目标语义转换为真实目标语言结构。Target AST/LIR 中不再存在未解析 intrinsic、源语言 semantic opcode 或 capability 决策：

- JavaScript 使用自己的 module/declaration/statement/expression/import/export 节点；
- `cpp` 使用自己的 translation-unit/include/namespace/declaration/type/statement/expression 节点。

名称分配、声明排序、runtime dependency pruning、临时值 canonicalization 和 target peephole pass 在这里完成。每个 pass 必须声明 preserved analysis 并接受对应 LIR verifier。

当前 LIR v15 已把函数 calling convention、source export、临时值、顶层/函数/statement/body/alternative 作用域声明、顶层布局、表达式/语句、当前 writable-call ownership/copy-in-out、强类型 comparison、Matlab matrix-operation kind/solve/shape、逐下标 selector identity 和逐节点 source mapping representation 前移。expression、statement、parameter、return、multi-target 与 C++ declaration 保存 `SymbolId`，`IdentifierInventory` 验证 identity/spelling 后生成目标保留字安全名称；不同 lexical scope 的同名 symbol 可保持可读 spelling，但 renderer 不能再以 spelling 推导绑定。每个 call argument 使用一个聚合 plan 保存 value/optional-forward/reference-box/copy-section 与 none/direct/element/section writeback，避免并行数组；`ComparisonForm` 明确选择 infix/dynamic/structural equality/identity/membership，`EvaluationForm` 与 call value/outcome 明确选择 JavaScript arrow IIFE/thunk 或 C++ reference lambda、first-result 和结果保存。`cpp` 二元 comparison 同样拥有两个稠密 operand temporary，保证左到右单次求值。`SourceSegmentPlan` 按 `LirNodeId` 稠密保存 source/origin，renderer 只能用 ID 查询并产生 marker。独立 representation verifier 在 ABI/resource planner 后重建 expression、statement、scope、comparison、matrix operation、selector、evaluation、writeback 和 source segment 计划。架构门禁禁止 renderer 恢复共享语义、按 spelling 查 binding、扫描 call argument、读取节点 location/line/origin 或自行选择 wrapper。完整独立 target AST、一般 RAII/copy-move/runtime ABI node、结构化 import/export/chunk 与 generated-only segment 仍按 TODO 继续前移。

### JavaScript LIR

Matlab 算术 identity 由共享强类型 `BinaryOperator` 传至两个目标私有 expression；compatible-size
运算携带 `BroadcastPlan`，矩阵乘法/求解/整数幂携带 `MatrixOperationPlan`。两个目标各自的
representation plan 据此选择 infix、reverse divide、power 或已验证的 Matlab runtime call。
renderer 只序列化已经选定的 helper symbol 与 operand，不读取源语言、shape 或源 spelling。

JavaScript LIR 负责：

- module/script、import、export、NPM/runtime dependency 和 chunk 规划；
- JavaScript 表达式/语句形状、临时变量、IIFE/closure 与求值顺序；
- JS number/BigInt/Array/TypedArray/object 表示选择；
- runtime helper 的稳定符号 ID、tree-shaking requirement set 和 source map segment；
- ECMAScript target/version capability 与宿主环境约束。

### cpp LIR

`cpp` LIR 负责：

- translation unit、include、namespace、declaration/definition ordering；
- 具体 C++ 类型、template、value/reference/ownership 和 ABI/runtime 调用；
- 临时对象、lambda、RAII、copy/move 与异常策略；
- C++17 capability、编译器兼容性和 runtime header/source dependency。

### Binding 与失败关闭

- backend capability pass 只读取 MIR 和目标配置，输出稳定诊断或目标 lowering plan。
- `IntrinsicId` 在 MIR→LIR 时通过后端稠密 binding table 解析为 `RuntimeSymbolId` 或专用 lowering opcode。
- 缺失 binding 在产生 LIR 前失败关闭；emitter 不再调用 binding registry，也不匹配源语言 builtin spelling。
- 目标暂不支持某种 MIR 语义时由 capability pass 报错，不能让另一个后端的能力影响结果。

LIR verifier 检查目标符号已解析、声明顺序、名称唯一性、runtime dependency 完整性、类型可表示性和 source map origin。Emitter 只能接收已经验证的对应 LIR。

### Backend SDK

为方便接入新输出语言，公共 Backend SDK 提供机制而不提供目标策略：

- 强类型 target pass pipeline 和 verifier 调度；
- `MirOpcode` legalization registry 与完整性检查；
- 稠密 intrinsic/runtime binding table 和 `RuntimeSymbolId`；
- target-independent diagnostic/source-origin/metrics/resource-limit 工具；
- 确定性名称分配、依赖集合和稳定排序算法；
- opaque target artifact 生命周期，核心驱动无需 include 具体 LIR；
- descriptor/catalog validation 与 backend conformance harness；
- target-only、其他后端禁用、安装和外部消费 CMake 测试模板。

SDK 禁止包含 JavaScript/`cpp` 关键字、类型映射或 runtime helper；这些均属于具体后端。

## 第五层：Emitter

Emitter 是确定性的序列化组件：

- 输入只能是一个目标的只读 LIR；
- 不执行名称绑定、类型推断、shape/alias 分析或源语言语义判断；
- 不访问源语言 AST/HIR，也不调用另一个后端；
- 不因 unordered container 迭代顺序产生不稳定输出；
- 只连接已经 materialize 的文本 chunk，不再格式化结构化目标节点。

同一 LIR、同一配置和同一 MPF 版本必须逐字节生成相同结果。

output-bundle assembler 在 emitter 外部根据同一批 chunk 的 origin 构建 source map v3，并复制目标 lowering 已规范化的 dependency manifest；因此 source map 不需要反向解析生成文本，emitter 也不需要知道输入文件身份。

## Pass 与 Analysis 基础设施

统一 pass manager 需要支持 HIR、MIR 和各目标 LIR，但通过不同的强类型 pipeline 实例阻止跨层误用。

每个 pass descriptor 至少包含：

```text
stable pass name
IR stage
required analyses
preserved analyses
run callback
thread-safety/determinism declaration
optional metrics hook
```

- `AnalysisManager` 以 IR revision 和 analysis identity 缓存结果；mutation 后精确失效未声明 preserved 的分析。
- verifier 在每次 lowering 后强制运行；开发/CI 可在每个 mutation pass 后运行，发布模式可按 pipeline policy 选择。
- pass instrumentation 记录耗时、峰值 arena 使用、节点/块/指令数量变化和诊断数，不默认输出用户源码内容。
- 支持确定性的 textual dump 和结构化 dump，供 snapshot、崩溃最小化和回归定位使用。
- 单函数 pass 可按稳定 function order 并行；module/symbol graph pass 必须声明同步策略。一个 compilation session 不依赖可变全局状态。

## Registry 与扩展 contract

### 前端 descriptor

目标 Frontend descriptor 负责 canonical identity、extension、probe、源语言版本范围、feature manifest、parser factory 和 AST→HIR + semantic-seed lowering factory。descriptor 不拥有名称 alias、compilation session，也不返回内部静态可变对象。

### 后端 descriptor

目标 Backend descriptor 负责 canonical target identity、configuration schema、TargetProfile factory、MIR capability、legalization pipeline、target semantic IR/LIR factory、LIR verifier 和 emitter factory。descriptor 不拥有目标名称 alias；核心只持有 opaque target artifact，不能依赖具体 LIR 类型。

### 版本和插件

- 当前内置 descriptor 继续使用显式静态 catalog，以链接时可裁剪和零初始化顺序风险为优先。
- 每次内部 descriptor contract 变更都直接更新 API version、所有 producer/consumer 和 catalog validator；不保留旧布局 adapter。
- 动态插件是独立里程碑：稳定 C17 ABI、size/version negotiation、allocator/ownership callback、线程模型、错误边界、签名和隔离策略完成前，不承诺二进制插件兼容性。

## CMake 与依赖方向

目标依赖图如下，箭头只允许向下：

```text
mpf-source
   ▲
mpf-syntax-common
   ▲
frontend-python  frontend-matlab  frontend-fortran  ...
   │                         │
   └──────────────► mpf-hir ◄┘
                       │
                 mpf-analysis / mpf-mir / mpf-pass
                       │
           ┌───────────┴───────────┐
           ▼                       ▼
 backend-javascript           backend-cpp
 capability/legalization      capability/legalization
 semantic IR + JS AST/LIR     semantic IR + cpp AST/LIR
           └───────────┬───────────┘
                       ▼
                  mpf facade / CLI
```

实际 CMake target 可为控制构建时间适度合并，但 include 和链接依赖必须保持这个方向。隔离测试需要从 compilation database、导出 target 和安装包三个层面确认禁用组件没有被编译或传递链接。

## 性能与资源模型

商业级性能不以单个微优化衡量，而以可重复的预算和回归门禁衡量：

- SourceManager 保持源码一次拥有，token/AST 优先引用 span/string view；
- 语言 AST 已使用 session PMR arena 和稠密 ID；HIR/MIR/LIR 继续按阶段迁入 arena/side table，避免递归 shared ownership；
- interner、type/shape uniquing 和 binding table 查询应接近 O(1)；registry 查询不在热路径分配；
- parser、lowering 和 verifier 目标复杂度为源码/节点线性或明确记录的 `O(n log n)`；禁止未记录的嵌套全表扫描；
- 对深度、节点数、token 长度、常量折叠规模和生成输出设置可配置 resource limit，耗尽时稳定诊断；
- benchmark corpus 分小文件延迟、大文件吞吐、深层控制流、大 shape/array、跨函数图和批量并发 session；
- CI 运行小文件、吞吐、深 CFG、大 shape、函数图和八路并发场景，按版本化 JSON baseline 阻断延迟、吞吐、峰值 arena 或产物大小回退；
- 默认构建不得因统计/IR dump 产生明显成本，instrumentation 可配置开启。

## 诊断、source map 与可观察性

- 每次 lowering 保存 origin chain，诊断优先定位最接近用户语法的 span。
- 内部 verifier 失败使用稳定内部诊断前缀，并包含 stage/pass/node identity；发布模式不得崩溃或输出未处理异常。
- JavaScript 与 `cpp` 都从 serialized LIR chunk origin 生成 source map v3，不从 emitter 反向猜测；CLI `--source-map` 可单独写出标准 JSON map。
- `CompilationReport` 包含 source 大小、总耗时、峰值 arena、各阶段耗时/节点数，以及 `mir_optimization` 的 folded/retired/removed/propagated/canonicalized 与 instruction/block before/after 计数；JSON 使用同一数据且默认不记录源码正文。

## 测试与质量门禁

每个层级都有独立门禁：

- 前端：lexer/parser corpus、AST verifier negative tests、版本门控、fuzz 和 resource exhaustion；
- AST→HIR：每种语言的 normalized HIR golden，证明不同表面语法得到相同语义；
- HIR/MIR：verifier negative tests、CFG/type/shape/storage/effect 单元测试、deterministic dump；
- MIR pass：before/after golden、语义差分、analysis invalidation 和 idempotence；
- 后端：capability matrix、MIR→LIR golden、缺失 binding、LIR verifier negative tests；
- emitter：format golden、语法/编译、source map、逐字节确定性和 runtime dependency；
- 端到端：源 runner、Node.js、生成 `cpp`、oracle 差分；目标禁用/安装/外部消费隔离；
- 工程：ASan/UBSan、clang-tidy、format、覆盖率、CodeQL、跨平台和 fuzz smoke；
- 性能：时间、峰值内存、产物大小和 runtime 性能基准，不允许无审计回退。

新增语言或后端必须复用 conformance harness；只增加几个成功样例不算完成接入。

## 分阶段迁移与完成定义

重构必须保持现有行为，禁止一次性替换全部 parser/emitter。

1. 建立强类型 ID、arena、origin、dump、verifier、pass/analysis manager，不改变输出。
2. 为三个语言建立独立 arena AST 和 AST→HIR visitor；生产 artifact 不再保存共享 syntax tree。
3. 冻结 HIR contract；Analyzer 改为读取 HIR、把结果写入语义 side table，不再原地混合语法和语义。
4. 建立 MIR CFG、type/shape/storage/effect model；先以无优化 lowering 保持当前输出，再启用保守公共 pass。
5. 为 JavaScript 与 `cpp` 分别建立 capability→legalization→target semantic IR→representation/ABI lowering→target AST/LIR→verifier 子管线；把 emitter 中的语义判断、runtime 扫描、binding lookup 和 target validation 迁出。
6. Emitter 只保留纯 serialized-chunk 序列化器；架构门禁禁止 `Program` 直通后端路径，并由 output-bundle assembler 生成 source map/dependency manifest。
7. 拆分 CMake target、安装组件和 extension conformance harness，补齐 fuzz、资源、性能、确定性与隔离门禁。

只有同时满足以下条件，TODO 才能把“商业级多层 IR 与可扩展前后端”标为完成：

- 五层均为独立强类型数据模型并有 verifier；
- 三个现有前端只通过 AST→HIR contract 接入；
- Analyzer/公共 pass 只依赖 HIR/MIR，不依赖目标；
- 两个后端只读取 MIR，分别经过 capability、legalization、私有 semantic IR 和私有 AST/LIR；
- 两个 emitter 只读取各自 LIR，且不按源语言分支推断语义；
- 旧共享 `Program` 不再从 parser 一路传到 emitter；
- 全量差分、隔离、跨平台 CI、sanitizer、静态分析、覆盖率、fuzz smoke 和性能门禁通过；
- 文档中的支持矩阵、extension contract、诊断和实际代码一致。
