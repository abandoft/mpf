# MPF 架构

本文描述 0.4.0 发布版的实际架构。CMake 同时固定 C17/C++17 标准基线，但当前生产实现和公共 API 使用 C++17；`cpp` 是 C++ 输出目标的代码身份，C++17 是当前生成标准。最终职责、性能模型和后续迁移验收条件见 [商业级编译器管线方案](COMPILER_PIPELINE.md)。

## 当前状态边界

生产驱动已经切换为五层路径，旧的共享 `Program`→emitter 直通入口不存在：

- 四个 statement parser 通过 `FrontendAstBuilder<LanguageTag>` 直接产生编译期互不兼容的语言 AST artifact；递归表达式解析后立即驻留，statement body/root 只保存稠密 `AstNodeId`，错误恢复也只发布可达节点。顶层 arena 容器使用 parser session PMR resource，不存在跨语言递归 syntax tree 或 parse 后整树复制；随后显式运行 AST verifier，AST→HIR visitor 原子产出窄结构 HIR 与按 `HirNodeId` 稠密、绑定 revision 的 `SemanticTable` seed；
- Analyzer 和 HIR pass 只处理 HIR 与显式 side-table 输入；独立的只读 name/flow pass 分别生成 revision-bound 稠密 `NameTable` 与 `FlowTable`，负责 lexical scope/symbol/builtin、reachability/termination 和不可达诊断；Analyzer 在运行任何 pass 前验证 frontend seed，以 `ScopeId`/`SymbolId` 稠密状态消费两表并原位完善 `SemanticTable`。HIR 节点不保存 type、shape、binding、call association 或 assignment-pattern 镜像；HIR→MIR 只从 semantic side table 读取这些 facts，并从 name table 固化 storage/function 的 `SymbolId` identity；
- MIR lowering 以 `MirExpressionId`/`MirStatementId` 建立带零号哨兵的稠密 expression/operation arena；child、body/alternative 和 roots 只保存强类型 ID，每个节点绑定 resident instruction，递归 HIR 兼容树不再驻留。revision-bound `OperationAttributeTable` 按 MIR ID 稠密保存 spelling、comparison/binding/intrinsic、索引/调用策略和强类型 assignment facts，结构节点不再镜像 HIR `ValueType`/extent/intent/return payload。if/loop/loop-else/`break`/`continue`/`SELECT CASE` 具有真实 CFG；conditional、逻辑短路和 comparison chain 进一步产生分支专属 block、`truthiness`/`compare`、typed block argument 和 edge actual，使未选分支不进入执行路径。变量读取、声明/赋值和 section writable call 产生 `load`、`allocate`、`store`/`store_indexed`、`copy`/`writeback`。shape stride、storage view/lifetime/intent/optional 与 tuple/function/reference type/shape 签名均使用强类型 ID；全局 storage 以 NameTable 派生的 `SymbolId` 跨函数共享身份；
- 每个 user-call argument 是单一 region contract，保存 type、storage/root、intent、transfer、view、lifetime 和 writability，transfer 区分 value/borrow/copy/optional-forward/omitted；copy-out 与 copy-in/out 在 call 前后形成不同 operand arity 的 temporary/writeback 指令。alias relation 和 instruction/function/call effect 不内嵌 MIR，而由 revision-bound、可缓存的 `AliasEffectTable` 计算，显式 load/store/copy/writeback 直接贡献 read/write/allocate。MIR verifier 检查 attribute revision/density、expression/instruction metadata、operation ownership/可达性、lazy merge、CFG/dominance、type/shape/storage、函数签名和 call transfer/lifetime；一次构建的稠密 call/value/block-argument 索引避免验证成本退化；alias/effect verifier 独立检查 storage root、稀疏 alias、read/write set、跨函数 fixed point、call-site 参数实例化和 writable overlap；
- JavaScript 与 `cpp` 通过 backend descriptor API v5 显式接收并验证 MIR 与 alias/effect facts，再分别执行 TargetProfile、逐 opcode legalization、capability、私有 semantic plan、独立 LIR/pass/verifier；两个目标 LIR v11 保存 lowering 后的 ABI、scope/declaration、CSR 临时资源、module/translation-unit 拓扑、source export 与目标 expression/statement representation。强类型 `ComparisonForm` 固化 equality/ordering/identity/membership，单一 `CallArgumentPlan` 同时固化传递 ownership 与 writeback，`EvaluationForm`/call value/outcome 固化 comparison/lazy/writable-call 的 IIFE/lambda/thunk 和结果保存；按 `LirNodeId` 稠密的 `SourceSegmentPlan` 固化每个目标节点的 source/origin；
- renderer 不再根据 section AST 猜测 copy/writeback、扫描 call argument 选择 wrapper、读取源 parameter intent、递归扫描声明或动态分配临时名，也不再读取 `StatementKind`、`ExpressionKind`、`ValueType`、assignment pattern、源 index/shape、节点 location/line/origin、builtin binding、call transfer 或动态参数集合，且不接收 `TranspileOptions`、runtime requirements、函数图；两个目标 runtime source catalog 与 representation planner/verifier 均为独立编译单元。尚未结构化的一般 RAII/copy-move/runtime-call node 仍在对应 target renderer 序列化；核心只持有 opaque target artifact，两个 emitter 只调用 `serialize_chunks`。
- facade 从最终 LIR origin 构建 source map v3，并公开 dependency manifest 和包含阶段耗时/节点/峰值 arena 的编译报告。

0.4.0 在 flat MIR 执行语义基线上接入独立 TypeScript descriptor、statement lexer/parser、`typescript::ast` 与双目标纵切面；explicit-only export policy 由 frontend semantic profile/side-table 贯通 MIR function 和 JavaScript LIR ABI，不在 emitter 中按源语言猜测，双目标 artifact schema 随 source-export contract 升至 LIR v11。此前交付的窄 HIR + semantic seed、纯 emitter、source map、资源防护、fuzz、版本化性能门禁和静态一般 rank 主链路保持有效。独立 target AST、一般 RAII/copy-move/runtime ABI node、完整四语言官方 grammar、动态 rank/广播、精确 N 维 storage region overlap 与稳定插件 ABI 仍未完成。所有边界逐项记录在 [TODO](../TODO.md)。

## 设计原则

- 前端彼此隔离，只负责源语言语法和语义；禁止在解析器中直接拼接目标语言代码。
- HIR 表达规范化源语义，MIR 表达目标无关执行语义；后端不得从源语言身份猜测行为。
- 不支持或无法保持语义的结构必须产生诊断，禁止静默降级。
- 公共 API 只位于 `include/mpf/`；内部头文件不形成兼容性承诺。
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
HIR→MIR → MIR verifier → alias/effect analysis + verifier
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
  → MIR：类型化 CFG、值、shape、storage；alias/effect 位于独立分析表
  → JavaScript LIR / cpp LIR：目标专属 capability、binding 与 lowering
  → emitter：确定性序列化
```

五层已有不同强类型表示和 verifier。生产 AST 不包含共享 syntax tree，最终 emitter 不包含 lowering；name、flow 和 Analyzer 语义事实分别只有 `NameTable`、`FlowTable`、`SemanticTable` 一个 owner，三者都以 HIR revision 拒绝 stale/missing facts。HIR 已删除宽语义字段及共享 syntax lowering 旁路；MIR 已删除递归兼容 ownership和宽语义 payload，结构节点、revision-bound `OperationAttributeTable` 与驻留 type/shape/storage 表各自只有一个职责。详细 contract 和禁止依赖见 [COMPILER_PIPELINE.md](COMPILER_PIPELINE.md)。

0.0.8 将目标无关编译和目标能力验证彻底分层；0.0.9—0.2.3 逐步建立 section、SourceManager、差分、tokenized parser、多输出、函数图、Fortran procedure、引用与 section copy-in/copy-out；0.2.4/0.2.5 建立 Fortran argument association 与完整当前 optional intent；0.2.6 将通用 call keyword/default 元数据复用于 Python positional-only/keyword-only association；0.2.7 将同一多目标赋值 IR 扩展到 Python 固定序列解包；0.2.8 将 C++ 目标身份统一为 `cpp`；0.2.9 抽取递归 `AssignmentPattern` 与 `ValueMetadata`；0.3.0 建立格式、静态分析、覆盖率和安全扫描门禁；0.3.1 新增结构化 SELECT/CASE IR、任意分支合流和双后端单次求值 lowering；0.3.2 新增显式 Python comparison-chain/conditional AST、类型关联验证和双后端惰性单次求值 lowering；0.3.3 建立对称的前后端 descriptor/registry、稳定 intrinsic ID 与目标代码绑定表。Analyzer 对不同源语言分别确认 association、unpacking、selection 与表达式语义，后端不解析源语言参数或控制结构语法。通用 Analyzer 不接收 `TargetLanguage`，任一目标均不依赖另一目标的生成物。

0.3.2 的 Python comparison-chain 节点直接保存操作数序列和操作符序列，conditional 节点保存 condition/true/false 三个子节点。0.3.6 将比较操作符升级为强类型 enum，并加入 equality/ordering/identity/membership 分类；Analyzer 分析操作数 type/element/shape/tuple metadata，但不选择目标 lowering。自 LIR v9 起，JavaScript arrow IIFE 与 C++ reference lambda 的选择由各自 `EvaluationForm` 在 representation pass 固化；LIR v10 进一步为 `cpp` 二元比较分配两个 CSR operand temporary，避免依赖 C++17 函数实参求值顺序，renderer 只物化已验证的目标结构。

0.3.4 把 frontend descriptor 升级到 API v5、backend descriptor 升级到 API v4；0.3.5 将 backend descriptor 升级到 API v5，使 capability 与 lowering 同时接收 revision-checked alias/effect facts。前端 manifest 声明 minimum/maximum language version、feature bitset、resource contract、AST schema、确定性和 reentrancy，并通过工厂创建独立 parser session；后端 manifest 声明目标标准、artifact/configuration schema、runtime license/supply-chain、TargetProfile 和 legalization factory。catalog 保持静态只读、无全局构造器自注册，conformance harness 会重复 lowering/dump/emit 并比较确定性。当前 contract 用于编译期内置组件，不是稳定动态插件 ABI；接入步骤见 [扩展指南](EXTENDING.md)。

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

隔离测试会为 javascript-only、cpp-only 和 core-only 分别创建全新构建树，检查 compilation database 中不存在禁用后端源码，执行可用/不可用目标诊断，安装对应组件，再由外部 CMake 消费者验证导出依赖。

生成某一目标只读取公共 IR 和该目标组件；任何后端都不读取另一后端的源码输出。废弃归档内容不属于依赖图，也不得由 CMake 或安装包引用。

## 目录边界

- `core`：公共入口、诊断以及编译会话生命周期。
- `source`：SourceManager 拥有多份源码、稳定文件表、UTF-8 位置、行表和跨度。
- `lexer`/`lexers`：公共表达式 token/scanner、语言独立词法规则、共享 `BasicStatementToken` 载体，以及带 byte span/源位置的四语言 statement lexer。
- `compiler`：递归 assignment pattern/value metadata、Pratt 表达式 parser、轻量 statement identity、通用 HIR 函数依赖图、稳定 intrinsic ID 和代码绑定验证；不定义跨语言 syntax `Program`/`Statement`。
- `ir`：强类型 ID、semantic profile、HIR、MIR lowering、独立 MIR opcode contract/verifier、pass/analysis manager 与确定性 dump。
- `semantic`：目标无关的作用域、名称绑定、builtin 遮蔽、确定赋值、循环上下文、不可达代码、表达式分支类型、标量/元素类型、矩形 shape、动态 extent、slice 长度、section conformability、逐维静态越界和 rank。
- `frontends`：统一 descriptor/registry、只共享 arena 生命周期机制的 `FrontendAstBuilder`，以及每种语言独立 logical-source normalizer 与递归下降 statement parser；parser 消费 statement token/span，把表达式跨度交给 Pratt parser 并立即驻留到本语言 arena。当前迁移覆盖已支持子集，完整官方 grammar 仍按各语言里程碑扩展。
- `backends`：descriptor v5、revision-checked alias/effect 输入、TargetProfile/legalization、目标 intrinsic binding、独立 capability/semantic plan/LIR/pass/verifier、resource/layout/representation planner、target renderer、目标 runtime source catalog 与纯 serialized-chunk emitter，以及仅供后端使用的保留字/冲突安全名称和 CSR temporary plan。Python loop-else 使用每层独立完成标志；数组访问的 nested/matrix/section 形式由目标 `ExpressionPlan` 固化，并消费 shape/bounds/base/negative-index/column-major 元数据。生成 C++ 放入 `TranslationUnitPlan` 固化的 `mpf_generated` namespace，runtime 放入独立 `mpf_runtime` namespace。
- `cli`：文件和参数 I/O，不包含编译语义。

## 正确性策略

每一项语言能力至少包含：成功样例、边界样例、拒绝样例，以及两个目标后端的行为测试。可执行成功样例进入单一 corpus manifest；JavaScript 使用 Node.js 做语法/执行验证，C++17 生成物使用顶层相同 compiler/generator 严格编译执行，源 runner 可用时直接参与同一结果比较。诊断码、JSON schema 与 CLI 退出状态是工具集成契约，删除或重定义需记录在变更日志；详见 [测试文档](TESTING.md) 与 [诊断文档](DIAGNOSTICS.md)。

## 依赖策略

核心库当前不依赖第三方组件。未来引入解析器生成器或 NPM/runtime 组件前，需记录版本、许可证、供应链固定方式和跨平台影响。标准数值函数分别映射到 ECMAScript `Math` 与 C++ `<cmath>`；仅当目标语言缺少等价语义时才引入小型、可审计的 runtime。
