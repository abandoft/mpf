# 扩展前端、后端与代码绑定

0.5.5 使用对称的 descriptor/registry 架构接入四种内置源语言和两个输出目标。当前核心驱动执行“选择 descriptor → 创建 parser session → parser 直接构造语言 arena AST → AST verifier → AST→窄 HIR + semantic seed → HIR/seed verifier → NameTable/FlowTable → Analyzer + normalized storage-region side table → flat MIR value/operation arena + revision-bound expression/statement/instruction attributes + lazy/memory CFG → 共享 MIR 默认优化 + 逐 pass verifier → 优化后区域化 alias/effect → CFG memory-dependence fixed point + verifier → capability/legalization → 私有 semantic plan/LIR → LIR verifier/dump → printer”，不按具体语言或目标硬编码分派。TypeScript 证明了同一扩展边界既可承载独立 token stream/arena 和 explicit export policy，也可通过 semantic profile 选择 lexical-block scope model，而无需修改 emitter 分派。新增源语言只负责产生同一 `MemoryAccess` contract；RAW/WAR/WAW、unknown barrier 与 loop-carried 分析继续由公共 MIR 层统一完成，前端和目标后端都不得复制。当前 descriptor contract 面向同一源码树中的编译期组件，只接受 canonical name，并且不承诺跨版本 C++ 布局或动态库插件 ABI。

本页记录当前可执行的 frontend API v6/backend API v6 接入方式以及尚未完成的动态插件 contract。语言 AST artifact、direct arena builder、窄 HIR + frontend semantic seed、Analyzer 直写 side table、profile 驱动 `NameScopeEdges`、独立 flow/alias-effect、MIR resident instruction + ID arena、共享默认优化、call argument borrow/copy/optional-forward/normalized-region contract、按 `InstructionId` 稠密的区域化 `MemoryAccess`、当前控制结构 CFG、`SymbolId` target inventory、LIR v20 lexical `ScopePlan`、Matlab array-literal/broadcast shape source、matrix-operation/solve/condition-policy/structure-policy plan、逐下标 selector/extent identity 与 Semantic v9→MIR v14→LIR v20 的 overwrite/resize/grow/erase mutation contract、目标 lowering 和纯 serialized-chunk emitter 已实际进入生产路径；静态已知 shape 的同根 N 维 selector overlap 与直接/跨调用 memory effect 已由公共 Analyzer/MIR/alias 层完成，动态 `end` 使用强类型 runtime-axis/runtime-linear contract，Matlab local-function compatible-size 使用 runtime-operands broadcast contract，shape-changing write 统一声明整个 aggregate root 的写 effect。`ArrayLiteralPlan` 还为 shaped-empty literal 固化不可由嵌套容器反推的 shape，JavaScript 通过 descriptor 消费该计划，C++ 通过静态 shape 参数消费对应契约；矩形 Matlab solve 必须显式选择 basic-solution-with-warning policy，方阵必须选择 square-continue-with-warning 与 classify-real-square policy，目标不得从 helper 名称或 shape 猜测数值条件行为。一般 NDArray/typed-array ownership、跨语言动态 shape 数据流、pointer/view association 与 region 组合、完整官方 grammar 及独立 target AST 仍不是已经完成的扩展接口。权威边界见 [商业级编译器管线方案](COMPILER_PIPELINE.md)。

新增求解目标或数值 runtime 时，必须消费 `MatrixStructurePolicy`，并对当前 real square plan 接受 `classify_real_square`、对 rectangular plan 接受 `none`；不得在 lowering 后重新扫描源 AST 或根据 helper 拼写决定结构检测。该策略要求 diagonal、triangular、带相邻行主元的 tridiagonal、symmetric-positive-definite Cholesky 与 dense fallback 行为一致，并为每个 kernel 提供正/转置求解和条件估计。增加 Hermitian、sparse 或 complex 分派时，应先建立对应 numeric/storage ABI，再扩展共享枚举和 verifier，随后分别实现 JavaScript/`cpp` LIR binding、目标独立 runtime、损坏计划测试、差分、source map、fuzz 与性能门禁。

新增源语言逻辑运算符时，Analyzer 必须为每个 operator/context 选择 `LogicalEvaluation`，不能只复用 `BinaryOperator::logical_and`/`logical_or`。eager-elementwise 需要明确 result type、shape/broadcast 与 element conversion；boolean-short-circuit 需要明确 operand truthiness 和 boolean merge；operand-short-circuit 则必须合流源 operand 值。MIR verifier 与目标 LIR verifier 应独立重算策略，目标 representation planner 只选择 helper/infix/lambda/thunk，renderer 不得从 token、`SourceLanguage` 或 child type 恢复求值策略。新增数组 truthiness 还必须分别覆盖 empty、全非零、含零、动态 operand、source map、双目标差分、fuzz 与性能场景。

## 设计约束

- descriptor 数据使用静态存储期和 `std::string_view`，注册表不取得所有权，也不在热路径分配。
- 注册顺序不能改变检测结果；扩展名必须唯一，内容探测最高分唯一，否则自动检测失败关闭。
- 不使用全局构造器自注册，避免链接器裁剪、初始化顺序和多动态库状态问题。
- 前端只能生成自己的语言 AST artifact、窄 HIR、semantic seed 和诊断，不能调用 Analyzer/MIR/backend/emitter 或拼接目标代码；接入核心只能通过 AST verifier 与 AST→HIR lowering。
- Analyzer 只解析源语言语义，不选择目标 lowering。
- 每个后端直接读取同一份经共享 pipeline 验证并优化的 MIR 和基于该 revision 的 alias/effect，不读取其他后端的输出。
- 所有未知语法、不可保持的语义和缺失绑定必须在生成前产生稳定诊断。

## 接入新的源语言

一个内置前端由 `FrontendDescriptor` 描述：

```text
language identity
canonical name
filename extensions
allocation-free content probe
language version + feature bitset + resource contract + AST schema manifest
source-specific intrinsic binding table
parser-session factory
language AST verifier
AST-to-HIR + dense semantic-seed lowering callback
```

接入步骤：

1. 在公共 `SourceLanguage` 中加入稳定身份；名称使用语言名，不携带实现标准版本号。
2. 在 `src/frontends/<canonical-language>/` 中实现 expression/statement lexer、logical-source normalization、statement parser、frontend factory 和语言专属 PMR arena AST；节点类型必须与其他语言编译期不兼容，不能把共享 syntax `Program` 放进 artifact。只有真正跨语言、目标无关的前端设施才能进入 `src/frontends/common/`。
3. 提供 descriptor factory 和独立 parser-session factory；扩展名和探测规则归前端所有，解析选项、arena、资源上限和请求 feature 通过 `FrontendParseOptions` 传入，并声明版本范围、能力与 AST schema。
4. 在 `src/frontends/common/registry.cpp` 的静态 catalog 增加一项，并将组件源码加入 `mpf-core`。
5. 由 descriptor 显式选择一组有序 spelling → `IntrinsicId` 表；只有源语言确实提供相同全局拼写时才选择共享数学表，TypeScript 一类语言可以完全不选。语义相同的函数复用已有 ID，语义不同的函数必须新增 ID。
6. 提供 AST verifier 和 AST→HIR lowering；每个有效 `HirNodeId` 必须同时拥有同 revision 的 semantic slot。运行 `run_frontend_conformance` 验证 descriptor、HIR/seed 完整性以及重复 parse/lowering 的逐字节确定性。
7. 增加 canonical 名称/扩展名冲突、旧缩写拒绝、探测优先级、parser 成功/拒绝、双后端行为和差分测试。
8. 更新语言支持矩阵、诊断索引和版本目标；CLI 帮助会从 registry 自动枚举 canonical name。

`hir::LoweringResult` 中的窄 HIR + `SemanticTable` seed 是当前前端扩展边界。新语言特性若不能由这对产物无损表达，应先扩展公共 HIR/semantic/MIR contract 和两个后端的 capability contract，不得借用字符串标记或语言名布尔字段绕过语义层。新表面语法必须留在语言 AST；跨语言结构进入 HIR，规范语义 facts 进入 seed。

## 接入新的输出目标

一个内置后端由 `BackendDescriptor` 描述：

```text
target identity
canonical name
target standard + artifact schema + determinism/reentrancy manifest
TargetProfile + dense MIR legalization table
intrinsic code-binding lookup
capability validator
MIR + revision-checked alias/effect facts-to-private-artifact lowering
artifact verifier
emitter/printer
```

接入步骤：

1. 在公共 `TargetLanguage` 中加入稳定身份。
2. 在 `src/backends/<canonical-target>/` 新建不重复目标前缀的 `backend`、`bindings`、`lir`、`lowering`、`renderer`、`runtime`、`validator` 和 `emitter`，并建立独立 CMake target；只依赖 `mpf-core` 和 `src/backends/common/` 提供的允许设施。
3. 在 `src/backends/common/registry.cpp` 增加目标 metadata 与条件 factory，在 facade 中加入对应构建选项和链接边界。
4. 为每个 `IntrinsicId` 提供显式 binding；不支持的项保留 `unavailable`，让 `MPF0004` 在 emitter 前失败关闭。
5. 运行 `run_backend_conformance`，以共享默认 pipeline 产出的同一 MIR 和 revision-checked alias/effect table 验证 descriptor/profile/legalization/binding、重复 lowering、artifact verifier 和逐字节确定性；后端不得自行复制公共 folding/DCE/CFG pass。
6. 增加 target-only、其他后端禁用和 core-only 的全新构建/安装/外部消费测试，确认 compilation database 不包含禁用目标源码。
7. 增加生成代码的语法、编译或执行门禁，并记录 runtime、依赖、许可证和供应链策略。

后端不得把另一个目标当作中间语言。目标间可以共享经过审计的名称改写、IR visitor 或算法，但不能共享生成产物或目标语法字符串。

## 当前生产生命周期与最终收敛目标

frontend descriptor 当前已经提供以下生命周期：

```text
create frontend session
  → normalize/lex/parse language-owned AST
  → verify AST
  → lower AST to narrow HIR + revision-bound dense semantic seed
  → verify HIR and seed as one ownership transfer
  → destroy frontend session/AST arena when no longer needed
```

当前内置前端由 `CompilationSession` 提供 SourceManager/arena/资源上限，每次 parse 经 descriptor factory 创建独立 parser session；feature 请求不是已声明能力的子集时失败关闭。

新前端只能依赖 source、syntax-common、HIR 与 semantic-facts contract。它负责版本范围、feature manifest、AST verifier 和 AST→HIR/seed conformance；不能 include Analyzer/MIR pass、目标 LIR 或 backend 头文件。

backend descriptor 当前已经提供以下生命周期：

```text
read-only MIR + verified alias/effect table
  → target capability pass
  → legalization registry
  → target semantic IR + resolved runtime bindings
  → representation/ABI lowering
  → target AST/LIR + target passes
  → verify target AST/LIR
  → renderer materializes source-origin chunks
  → pure emitter serializes code
  → output bundle adds source map/dependency manifest
```

当前 descriptor 已覆盖 configuration schema、runtime component/license/origin/integrity 供应链清单、semantic LIR dump 和 code emitter；公共 `TranspileResult` 提供 code、source map v3、确定性 dependency manifest 与阶段报告。项目与内联 runtime 使用 MIT 许可证，内置组件的 manifest 以 SPDX `MIT` 标识；接入外部 runtime 时必须记录其真实许可证，不能沿用项目默认值。

新后端只依赖 MIR/analysis/pass contract 和允许的 backend-common 设施。capability 与 lowering 必须先拒绝 stale/非稠密 `OperationAttributeTable`，再拒绝 revision、inventory 或 fixed-point verifier 不一致的 alias/effect facts；类型、shape、layout、函数签名和 storage 只能从驻留强类型 ID 查询，不得恢复 flat 节点的宽 `ValueType`/extent 镜像。MIR lazy CFG 已确定 conditional/逻辑/比较链的求值边，canonical `for` CFG 已确定 update/continue/break 边，`CallArgument` 和显式 `copy`/`writeback` 已确定 value/borrow/copy/optional-forward/omitted 语义；新后端必须把这些 contract 复制到自己的 semantic IR/LIR，再选择目标表示，不得在 printer 中重新推断。函数 parameter passing/module/export/recursive declaration、Program/Function/statement/body/alternative scope declaration、所有 renderer 所需临时值，以及 module/translation-unit 的 banner/directive/include/namespace/runtime fragment/声明定义入口顺序，都必须在 target LIR 中显式规划并验证。名称必须以 `SymbolId` + spelling inventory 规划；printer 只能使用已分配 identity，不能按 spelling 重新解析 binding。每个目标 expression 还必须固化自己的 form/precedence/token、强类型 equality/ordering/identity/membership strategy、call ABI、逐实参传递形式、first-result、index/section 模式和具体容器表示；目标 `EvaluationForm` 负责选择 IIFE/lambda/thunk 等表示，但不能改变 MIR 已固定的单次求值顺序。printer 禁止读取共享 `ExpressionKind`、`IntrinsicId`、builtin binding 或 MIR transfer。推荐使用按 `LirNodeId` 的稠密 inventory，printer 不得接收编译 options、读取 function graph/runtime requirement、持有可变名称分配器或递归扫描声明/顶层语句。目标 runtime source 与 representation planner/verifier 应使用独立组件，不能夹在 renderer 主文件中。若类型必须引用表达式，保存强类型 LIR ID 与显式访问路径，并用一次性稠密索引解析，不保存跨容器裸指针或复制递归表达式树。每个后端拥有自己的 LIR 类型，不能把通用目标结构体加若干 target flag 当作 LIR，也不能调用其他后端的 capability、binding、lowering 或 emitter。

目标 descriptor contract 至少包括：

- 当前 identity、canonical name 和 configuration schema；
- 支持的 MIR feature/capability manifest；
- TargetProfile、MIR capability 和 legalization-registry factory；
- target semantic IR、representation/ABI lowering 和 target AST/LIR factory；
- target pass pipeline、LIR verifier 和 printer factory；
- runtime dependency、license 和供应链 manifest；
- descriptor API version、线程安全和确定性声明。

descriptor API 升级时必须同步替换 catalog producer/consumer、validation 和禁用组件 metadata 查询；内部 0.x C++ 布局不提供 adapter 或 ABI 兼容。动态插件需等待稳定 C17 ABI、allocator/ownership、version negotiation、错误边界、签名和隔离方案单独完成。

## Extension conformance harness

仓库已提供 `run_frontend_conformance` 与 `run_backend_conformance`：前者验证 descriptor、parser session、AST verifier、HIR/semantic seed verifier 和两者的重复 dump，后者验证 MIR、alias/effect table、binding、capability、lowering、artifact verifier、semantic dump 和重复输出。resource limit、source map、fuzz/performance 与安装隔离由全局 harness 补充；`examples/installed/frontend` 和 `examples/installed/backend` 会在 staging install 后作为真正独立的 CMake consumer 配置、构建和运行。

前端 conformance 至少验证：

- descriptor canonical name/extension/probe/version manifest；
- AST ID/span/ownership 和 verifier negative case；
- AST→HIR normalized golden、确定性和 resource limit；
- 禁止依赖 MIR、LIR 或 backend target；
- 至少一个成功、一个边界和一个拒绝端到端用例。

后端 conformance 至少验证：

- descriptor/configuration/capability manifest；
- 全量 intrinsic binding 的 direct/custom/unavailable 明确选择；
- capability/legalization 完整性、MIR→target semantic IR→LIR golden、逐层 verifier negative case 和缺失 binding 失败关闭；
- call argument transfer/lifetime/normalized region、writable overlap 失败关闭，动态/未知区域保持保守，目标 LIR 不重新携带源 call intent 或计算 overlap 并实际消费 transfer plan，以及 ABI/temporary/scope/declaration inventory 的 dense、arity、碰撞、类型 probe 和缺失负向测试；
- emitter 确定性、语法/编译、source map 与 dependency manifest；
- target-only、其他后端关闭、core-only 构建/安装/外部消费隔离。

一个新输出语言的推荐目录固定为：

```text
src/backends/<target>/
├── backend.*             descriptor, target profile and capability
├── bindings.*            intrinsic and runtime bindings
├── lir.hpp               target-owned semantic/rendered LIR
├── lir_planning.*        resources, ABI and scope planning
├── lir_representation.*  target representation decisions and verifier
├── lowering.*            MIR-to-target lowering
├── renderer.*            source-origin chunk materialization
├── runtime.*             target runtime catalog
├── validator.*           capability and legalization
└── emitter.*             pure serialization
```

接入新目标不应修改 MIR opcode 的既有含义、其他后端或核心分派；只有确属跨目标的新语义才先扩展 MIR contract。Backend SDK 负责生命周期、pass、诊断、origin、metrics、稳定名称和 conformance，具体后端负责所有目标策略。

## Intrinsic 与代码绑定

源代码中的 builtin/intrinsic 在名称绑定阶段转换为稳定 `IntrinsicId`，原始 spelling 仅用于源码身份和诊断。后端不再根据 `len`、`SIZE` 等源语言字符串猜测语义。

`CodeBindingKind` 定义四种目标策略：

| kind | 用途 |
|---|---|
| `symbol` | 直接调用目标标准库符号，例如数学函数 |
| `constant` | 直接生成目标常量表达式 |
| `custom` | 由目标 semantic/representation lowering 选择专用 runtime 或目标结构 |
| `unavailable` | 目标无法保持语义，生成前稳定拒绝 |

源 spelling 表采用有序静态数组和二分查找；JavaScript 与 `cpp` 绑定表按稠密 `IntrinsicId` 索引，查询为 O(1)。增加同义 spelling 不需要修改 Analyzer 或 emitter；增加全新 intrinsic 时，编译期完整性测试要求两个现有后端都明确选择 direct/custom/unavailable 策略，且所有选择必须在 renderer/emitter 前解析。

## 注册与性能门禁

注册测试至少覆盖：

- descriptor API version、必需回调和非空 canonical name；
- language/target identity、canonical name 和 extension 冲突，以及历史缩写拒绝；
- 大小写不敏感名称查询与禁用后端 metadata；
- 内容探测的唯一最高分和歧义失败关闭；
- 全量 intrinsic 的目标绑定完整性；
- 缺失 binding 不进入 emitter；
- 新组件关闭时不编译、不链接、不安装其实现源码。

大规模 grammar 的性能由 lexer/parser arena、增量源码视图、resource limits 和 benchmark corpus 约束；新组件必须加入相应 fuzz seed 与性能场景。registry 本身保持无锁只读、无堆分配。若未来提供运行时插件，将另行设计稳定 C ABI、所有权、线程安全、版本协商、签名与隔离模型，不能直接把当前内部 C++ descriptor 暴露为插件 ABI。
