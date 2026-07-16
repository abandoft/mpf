# MPF 持续建设路线图

本路线图同时记录 **0.4.1 已发布基线** 与后续交付目标的真实状态。历史交付细节见
[CHANGELOG.md](CHANGELOG.md)，当前可依赖的语言子集见
[docs/LANGUAGE_SUPPORT.md](docs/LANGUAGE_SUPPORT.md)。目标版本号表示语法/语义覆盖上限，不表示已经完整兼容 Matlab 2024、Python 3.14、Fortran 2023 或 TypeScript 6；TypeScript 已有独立、可执行且包含 lexical block/canonical `for` 的子集，但完整 grammar 仍未完成。

前后端商业级重构的权威设计见 [docs/COMPILER_PIPELINE.md](docs/COMPILER_PIPELINE.md)。路线图中的“多层 IR”只能指语言 AST、HIR、MIR、JavaScript LIR/`cpp` LIR 五层均拥有独立强类型数据模型、verifier 和实际生产调用路径；不能用共享结构体 alias、stage flag 或未被 pipeline 消费的空壳类型标记完成。

## 当前基线

| 项目 | 当前开发分支状态 |
|---|---|
| 实现与构建 | CMake 3.20+；配置固定 C17/C++17；当前生产源码和公共 API 使用 C++17，尚无稳定 C API |
| 输出目标 | 独立 JavaScript 与 `cpp` 后端；`cpp` 当前生成严格 C++17 translation unit |
| 前后端边界 | 四语言 parser session 直接构造并发布各自 arena AST artifact，不经过共享递归 syntax tree 或整树复制；生产驱动随后固定经过 HIR→MIR→目标私有 semantic plan/LIR→emitter，两个目标不读取彼此产物 |
| 扩展架构 | frontend descriptor API v5、backend descriptor API v5；parser session/feature/resource contract、configuration/runtime supply-chain manifest、AST verifier、TargetProfile、稠密 legalization、opaque artifact 和前后端 conformance harness 已接入 |
| IR 架构 | 四种语言使用编译期互不兼容的 PMR arena AST；AST→HIR 原子产出窄结构 HIR 与 revision-checked 稠密 `SemanticTable` seed，HIR 节点不再镜像 type/shape/binding/call/assignment facts；名称/作用域与控制流分别由 `NameTable`、`FlowTable` 持有，profile 驱动的 `NameScopeEdges` 为 function/statement/body/alternative 建立稠密 scope graph；MIR 使用 `MirExpressionId`/`MirStatementId` 稠密 arena 与 revision-bound `OperationAttributeTable`，结构节点不再镜像宽语义 payload；conditional/短路/comparison chain 和 TypeScript canonical `for` 已产生显式 CFG、typed edge merge 和 runtime-independent store；tuple/function/reference type/shape 签名、stride/view/lifetime 和单对象 call argument region/transfer 可验证；alias/effect 由独立 `AliasEffectTable` 持有；双目标 LIR v12 以 `SymbolId` 保存名称身份，并显式保存 lexical scope/declaration、ABI、source export、临时值、顶层拓扑、expression/statement、强类型比较、call ownership/writeback/evaluation 与稠密 source segment plan，emitter 仅序列化 |
| Python 最新能力 | relational/equality/identity/membership 比较链、右结合条件表达式、短路/惰性/单次求值；list/tuple 种类相等规则、singleton/reference identity、string/list/tuple membership；基础参数关联和递归固定序列解包 |
| Fortran 最新能力 | integer/character/logical `SELECT CASE`、范围/default、重叠检查和任意分支确定赋值合流 |
| 工程门禁 | 172 项内部测试；53 个差分 case、149 条工具完整环境执行路径；64 项 CTest；四语言 fuzz smoke、可选 libFuzzer、六场景版本化性能阈值、阶段报告；生产代码行覆盖率实测 89.39%（20566/23008），硬门槛 85% |
| 发布状态 | 0.x；没有长期 API/ABI 或完整语言兼容承诺 |

## 本轮商业级收尾验收（完成）

- [x] 四语言生产 artifact 使用编译期互不兼容的 PMR arena AST、稠密 ID、verifier、确定性 dump 和 AST→HIR visitor
- [x] 当前支持的分支、循环、loop-else、`break`/`continue` 与 `SELECT CASE` 使用 MIR basic block、terminator、block argument/edge actual；shape stride、storage view/lifetime/intent 可验证，独立 alias/effect table 提供保守 `alias_between` 查询
- [x] JavaScript/`cpp` representation、ABI、type/shape、名称、runtime 与 binding 决策在目标 lowering/renderer 完成；emitter 只序列化最终 chunk
- [x] 公共 output bundle 提供代码、source map v3、确定性 dependency manifest 与逐阶段 `CompilationReport`；CLI 支持 `--source-map`
- [x] 四语言/双目标 fuzz smoke、Clang/libFuzzer、资源耗尽、确定性重放、崩溃复现与最小化工作流落地
- [x] 延迟、吞吐、深 CFG、大 shape、函数图、八路并发、峰值 arena 和产物大小纳入版本化 JSON 发布门禁与 CI 报告

这里的“完成”只指上述架构与工程闭环；各语言官方 grammar、完整对象模型、动态 rank/广播、精确 N 维 overlap/alias 和稳定插件 ABI 仍由后续条目跟踪。

## 版本化交付与持续收敛

### 0.3.5：商业级前后端与五层编译器管线继续收敛（已发布）

0.3.5 以 16 条独立更新完成封版，交付窄 HIR + semantic seed、独立 name/flow/alias-effect side table、跨函数 MIR call contract 和双目标 LIR v9。下列已勾选项是该版本及此前版本的实际能力；未勾选项继续作为 0.4.2 及后续版本的架构 backlog。详细职责和禁止依赖见 [商业级编译器管线方案](docs/COMPILER_PIPELINE.md)。

#### P0：基线、指标与依赖规则

- [x] 以生成代码/诊断/差分 corpus、HIR/LIR golden 和绑定 `0.3.5` 的性能 JSON 固化生成输出、编译时间、峰值内存和产物大小基线
- [x] 增加生产 stage/include architecture test 与 javascript-only、cpp-only、core-only 链接/安装隔离，禁止 frontend→MIR/backend、公共 IR→backend 和 javascript↔`cpp` 反向依赖
- [x] 为重构设定 resource limit、确定性和性能回退阈值；CI 产出机器可读阶段指标与性能报告
- [x] 迁移以 feature-equivalent adapter 保持既有语言行为，现有单元、集成与差分 corpus 继续作为兼容门禁

#### P1：编译 session、ID、arena 与 pass 基础设施

- [x] 建立 thread-confined `CompilationSession` 基础，集中拥有 SourceManager、monotonic resource 和阶段节点指标
- [ ] 将 interner、诊断、配置和全部阶段 arena 所有权迁入 `CompilationSession`，并让 IR 容器实际使用 session resource
- [x] 增加强类型 `AstNodeId`、`HirNodeId`、`ScopeId`、`SymbolId`、`MirFunctionId`、`BlockId`、`InstructionId`、`ValueId`、`TypeId`、`ShapeId`、`StorageId`、`LirNodeId` 和 `RuntimeSymbolId`
- [ ] 节点采用阶段 arena/monotonic resource，分析采用按 ID 稠密 side table；禁止跨 arena 长期裸指针和递归 shared ownership
- [x] 建立 HIR/MIR/LIR 强类型 pass manager、`AnalysisManager`、revision、preserved-analysis 精确失效、逐 pass verifier 和耗时 instrumentation
- [x] 建立确定性的 HIR/MIR textual dump 与稳定 verifier 内部诊断
- [x] 增加确定性语言 AST dump、最终 LIR serialized-chunk inventory、阶段峰值 arena 内存统计及机器可读 `CompilationReport`

#### P2：语言专属 AST 与 AST→HIR

- [x] 建立 `python::ast`、`matlab::ast`、`fortran::ast` 独立 artifact root、稠密 `AstNodeId` inventory、source origin 和 descriptor verifier
- [x] 三种生产 artifact 使用编译期互不兼容的 `python::ast`/`matlab::ast`/`fortran::ast` 节点、PMR arena、稠密 ID 与 AST→HIR visitor；artifact 不再封装共享 syntax tree
- [x] descriptor parser 只向生产管线发布本语言 AST；四个 statement parser 直接构造语言专属 arena，comparison chain、多输出签名、source form/declaration attribute、TypeScript export 等表面信息随本语言节点保留
- [x] 建立窄 HIR 结构 contract，承接函数、参数/结果名称、调用、多目标标记、range、selection 与 slice/section；type/shape/binding/intrinsic/call association/assignment pattern 只进入同批 semantic seed
- [x] 以共享语义 profile 表达 truthiness、logical result、division、equality、layout 和 top-level storage；capability validator 不再按 `SourceLanguage` 分支
- [x] 四个 frontend descriptor 分别提供 AST verifier 与 AST→HIR + semantic seed lowering；统一 HIR/semantic verifier 在 Analyzer 前和生产路径逐 pass 执行
- [x] 增加 Python/Matlab 跨语言等价语义的 normalized HIR golden
- [x] 删除 HIR statement/expression 的宽语义镜像、共享 syntax→HIR 旁路和脱离 side table 的 HIR-only reindex；架构门禁阻止字段与旁路恢复
- [x] 共享 IR 不含带语言名的行为字段；新语言不得通过新增 `python_*`/`matlab_*`/`fortran_*` flag 接入

#### P3：独立 MIR 与公共分析

- [x] 建立 `Program`/`Function`/`BasicBlock`、block argument identity、稠密 instruction table 和单 terminator CFG 基础
- [x] 当前支持的 if/loop/loop-else/`break`/`continue`/`SELECT CASE` lowering 为真实 basic block、edge actual 与 block argument/phi equivalent
- [x] 建立当前 scalar/container 的 interned logical `TypeId` 稠密表
- [x] 扩展 tuple、function/reference 驻留类型签名；call-site 表显式关联 caller/callee、argument/result type、optional omission 和 writable storage，跨函数 verifier 检查 call/return、多结果及 OUT/INOUT 引用契约
- [x] 建立独立 `ShapeId`，表达当前 rank、静态/动态 extent 与 layout
- [x] 增加 row/column-major canonical stride、dynamic-rank 标记、section view storage 与 shape canonicalization
- [x] 建立 `StorageId` 和 `no_alias`/`may_alias`/`must_alias` 基础模型
- [x] 显式建模 view、optional parameter storage、copy-in/copy-out、writable actual、保守 overlap 与 storage lifetime；`CallSite` 使用单一参数对象而非并行数组，参数保存 type/storage/root/intent/transfer/view/lifetime/writability，精确 N 维 region overlap 仍由后续条目跟踪
- [x] 建立结构化 `EffectSet`：read、write、allocate、io、may-fail、control、external-unknown
- [x] HIR→MIR 显式固定左到右 evaluation order、conditional/逻辑/比较链短路、循环/选择 CFG、多结果，以及 load/allocate/store/copy/writeback runtime-independent semantic operation
- [x] MIR verifier 检查稠密表、函数/块/指令唯一所有权、函数内 edge、terminator arity、值唯一定义及 definition-dominates-use
- [x] MIR verifier 补齐 block argument/edge actual arity、定义顺序与 dominance、type/shape/storage metadata、view/lifetime/intent、函数签名、call/return、多结果与 writable reference 相容性；expression/operation arena 额外检查稠密 ID、resident instruction 对应、根可达性和唯一 ownership；独立 alias/effect verifier 检查稠密 inventory、storage root、稀疏 alias、instruction read/write/effect、函数 fixed point 与 call-site 实例化
- [x] AST→HIR visitor 同步构建按 `HirNodeId` 稠密索引、绑定 HIR revision 的 `SemanticTable` seed；Analyzer 在任何 pass 前校验并接管该表，全部输出经直接 accessor 写表，不注解或复制 HIR 语义；HIR→MIR 对缺失/陈旧 semantic/name side table 失败关闭
- [x] 参数关联引起的默认值克隆、optional omission 或实参重排会提升 HIR revision，并在结构规范化后将 HIR ID 与已分析 facts 一起紧凑重映射；规范化后再次执行 HIR/semantic verifier 和 HIR 节点资源上限
- [x] 将 reachability、statement termination、branch/body termination 和上下文深度拆到独立、只读 HIR、revision-bound 的稠密 `FlowTable`；不可达诊断由该 pass 独立产生并有 stale/dense negative test
- [x] 将 lexical scope tree、声明/参数/结果/循环变量、遮蔽、引用和 builtin 解析拆到独立、只读 HIR、revision-bound 的稠密 `NameTable`；Analyzer 删除 `unordered_map<string, Symbol>`，以 `ScopeId`/`SymbolId` 稠密状态消费 name/flow facts，结构规范化后重建两表
- [x] 按职责将 Analyzer 拆为控制/函数分析、表达式/调用/索引分析、内部 contract 三个编译单元，避免继续扩张单体源码
- [x] 将 alias/effect 从 MIR 结构与 lowering builder 拆成 revision-bound、可由 `AnalysisManager` 缓存的 `AliasEffectTable`；以 NameTable 派生的 `SymbolId` storage identity、call-site actual 和函数参数摘要计算跨调用图 fixed point，未知外部调用保守读写 unknown，双后端 descriptor API v5 显式接收并验证该表
- [x] 删除 HIR 中用于初始化兼容输入的 type/shape/binding/call/assignment 等宽语义字段；前端 conformance 同时验证 HIR/semantic seed 完整性与确定性
- [ ] 首批默认优化只包括经证明安全的 CFG cleanup、constant folding、dead-pure elimination、copy propagation 和 shape canonicalization

#### P4：JavaScript LIR 与纯 emitter

- [x] 建立 JavaScript `TargetProfile`、MIR capability 和在生产 lowering 中逐 instruction 执行的稠密 legalization registry
- [x] 建立不借用 MIR 生命周期的 JavaScript semantic lowering plan，提前解析 intrinsic binding、runtime feature、dependency 和源语义 profile
- [x] JavaScript LIR v4 固化 script/ESM、顶层 export、value/reference-box 参数 ABI 和按 `LirNodeId` 的 CSR 临时资源计划；renderer 不再读取 module option/源 parameter intent 或动态分配临时名
- [x] JavaScript LIR v5 增加 Program/Function `ScopePlan`，lowering 确定性规划声明顺序，renderer 不再递归扫描赋值；scope/ABI/temporary planner 与 verifier 为独立编译单元
- [x] JavaScript LIR v6 增加 `ModulePlan`，固化 banner、directive、runtime fragment 与顶层 body order；renderer 不再接收 options/runtime requirements，runtime source catalog 拆为独立编译单元
- [x] JavaScript LIR v7 增加独立 `ExpressionPlan`，固化 form/precedence/token、结构相等/惰性逻辑/比较链策略、direct/custom call ABI、逐实参 value/optional-forward/reference-box 形式、first-result 与 element/section selector plan；renderer 不再读取共享 expression kind、intrinsic、binding 或 transfer
- [x] JavaScript LIR v8 增加独立 `StatementPlan`，固化 declaration/assignment/print/return/control/function form、condition truthiness、value/reference-box 参数访问、一般 N 维默认数组初始化、assignment leaf、selector、range/loop-else 与 section 写回；`ExpressionPlan` 同步持有 index/slice metadata
- [x] JavaScript LIR v9 以单一 `CallArgumentPlan` 固化 value/optional/box ownership 与 direct/element/section writeback，以 `EvaluationForm` 固化 comparison/lazy/writable-call arrow IIFE/thunk；按 `LirNodeId` 稠密的 `SourceSegmentPlan` 在 renderer 前固定 source/origin
- [x] JavaScript LIR v10 以强类型 `ComparisonForm` 固化 infix/structural equality/identity/membership；私有 `Symbol` tuple tag 与 runtime helper 由 representation/runtime catalog 持有，renderer 只序列化已验证策略
- [ ] 把 JS 数据表示、calling convention、结构化 import/export/chunk 与 NPM/runtime manifest 完整固化到 semantic IR
- [ ] 完成独立 JavaScript AST/LIR 的结构化 module/script/import/export/chunk 节点；当前 expression/statement、临时值、IIFE/closure 和逐 `LirNodeId` source segment 已由 representation lowering 持有
- [x] 建立 JavaScript target pass pipeline，完成保留字安全名称分配、dependency canonicalization 和确定性排序
- [ ] 将结构化 import/export/chunk AST 与更细粒度 generated-only segment 从 renderer 前移到 LIR pass；当前支持的 statement/expression/operator/call/index、IIFE/closure、逐节点 source segment、参数访问、临时值、scope declaration 与 module layout canonicalization 已完成
- [x] 把 JavaScript emitter 中的源语言分支、runtime 扫描和 binding lookup 迁入 MIR→LIR lowering
- [x] 为 legalization、semantic lowering plan 和 AST/LIR 建立 verifier，emitter 公共入口只能接收 JavaScript LIR artifact
- [x] representation/type/shape/runtime/name 选择全部在目标 lowering/renderer 完成；emitter 仅 `serialize_chunks`，公共结果输出 source map v3 与确定性 dependency manifest

#### P5：`cpp` LIR 与纯 emitter

- [x] 建立 `cpp` `TargetProfile`、MIR capability 和在生产 lowering 中逐 instruction 执行的稠密 legalization registry
- [x] 建立不借用 MIR 生命周期的 `cpp` semantic lowering plan，提前解析 intrinsic binding、runtime feature、dependency 和源语义 profile
- [x] `cpp` LIR v4 固化 template、value/const-reference/mutable-reference/optional-reference、optional 具体类型、递归返回/前置声明 ABI，以及按 `LirNodeId` 的 CSR 临时资源计划；renderer 不再读取源 parameter intent 或动态分配临时名
- [x] `cpp` LIR v5 增加 Program/Function `ScopePlan` 与 `DeclarationPlan`，固化 concrete type/`LirNodeId` type-probe、pattern path、tuple index 和 fixed-shape nested type；renderer 删除声明扫描并以一次性稠密 expression view O(1) 查询 probe
- [x] `cpp` LIR v6 增加 `TranslationUnitPlan`，固化标准头文件、runtime/generated namespace、runtime fragment、前置声明/定义/入口顺序及 run/main 拓扑；renderer 不再读取 options/function graph/runtime requirements，runtime source catalog 拆为独立编译单元
- [x] `cpp` LIR v7 增加独立 `ExpressionPlan`，固化 form/precedence/target token、比较器、direct/custom runtime call、逐实参 value/optional-forward/copy-section、first-result、nested/matrix/section index 模式、list concrete type 与逐元素 widening；renderer 删除 expression kind/intrinsic/binding/transfer 解释
- [x] `cpp` LIR v8 增加独立 `StatementPlan`，固化 statement/control form、condition truthiness、direct/optional-value 访问、assignment leaf concrete type/widen、selector、range/loop-else、section replacement flatten/resize 与 return plan；`ExpressionPlan` 同步持有 index/slice metadata、reshape shape 和 call result policy
- [x] `cpp` LIR v9 以单一 `CallArgumentPlan` 固化 value/optional-forward/copy-in-out ownership 与 section writeback，以 `EvaluationForm`/call outcome 固化 comparison/lazy/copy-call reference lambda；按 `LirNodeId` 稠密的 `SourceSegmentPlan` 前移 source/origin
- [x] `cpp` LIR v10 以强类型 `ComparisonForm` 固化 equality/ordering/identity/membership，并为二元比较分配 CSR operand temporary 与 reference-lambda evaluation plan，保证 C++17 左到右单次求值
- [ ] 把具体类型、calling convention、value/reference/ownership 与 ABI 完整固化到 semantic IR
- [ ] 建立完整 representation/ABI lowering 和独立 `cpp` AST/LIR；translation unit/include/namespace/声明定义拓扑及当前 expression/statement/template/lambda/call ownership plan 已结构化，仍需把一般临时对象、RAII/copy/move 和 runtime ABI call 全部节点化
- [x] 建立 `cpp` target pass pipeline，完成保留字安全名称分配、函数依赖排序、dependency canonicalization 和确定性排序
- [ ] 将尚未结构化的一般 ownership/copy-move、RAII temporary 和 runtime ABI node 从 renderer 前移到 LIR pass；当前 writable call copy-in/out、reference lambda、statement/expression concrete form、optional/reference access、temporary、函数 ABI、scope/declaration 与 translation-unit canonicalization 已完成
- [x] 把 `cpp` emitter 中的源语言分支、函数排序、target validation、runtime 扫描和 binding lookup 迁入 MIR→LIR lowering
- [x] 为 legalization、semantic lowering plan 和 AST/LIR 建立 verifier，emitter 公共入口只能接收 `cpp` LIR artifact
- [x] representation/ABI/type/shape/name 选择全部在目标 lowering/renderer 完成；emitter 仅 `serialize_chunks`，公共结果输出 source mapping 与 dependency manifest
- [x] JavaScript 和 `cpp` LIR/CMake target/header 互不依赖，任一目标关闭时另一目标和 core 可独立构建、安装和消费

#### P6：descriptor、扩展 SDK 与门禁

- [x] Frontend descriptor API v5 提供 parser session factory、feature bitset、resource-limit contract、language AST verifier、AST→HIR factory、可验证 minimum/maximum language version、AST schema 与 determinism/reentrancy manifest；公共 API/CLI 支持 `LanguageVersion`/`--language-version`
- [x] Backend descriptor API v5 提供 TargetProfile、legalization、接收独立 alias/effect facts 的 capability/semantic IR/LIR factory、target verifier/dump/printer 与 artifact schema manifest
- [x] 增加完整 configuration schema 与可承载未来外部 runtime 的 license/supply-chain manifest；code/source-map/dependency output bundle contract 已落地
- [x] 建立 Backend SDK 基础：opaque artifact lifecycle、强类型 target pass、legalization registry、binding、origin、确定性名称和 conformance 工具
- [x] descriptor catalog validation 覆盖 API version、线程安全/确定性声明、profile/target 一致性、legalization 完整性和 callback 完整性；内置 catalog 保持静态只读
- [x] 提供可复用 frontend/backend conformance harness，自动验证 descriptor、verifier、binding、lowering 和逐字节确定性
- [x] 增加两个通过安装包 `find_package` 独立配置、构建和运行的 frontend/backend consumer/conformance 模板
- [x] 明确内部 C++ descriptor 仍不是动态插件 ABI；稳定 C17 plugin ABI 另立里程碑

#### P7：测试、性能与删除兼容路径

- [x] HIR/MIR/双目标 LIR 增加 verifier negative tests；HIR/MIR 增加 deterministic textual dump；后端 conformance 逐字节验证 emitter 确定性
- [x] source、AST/HIR/MIR/LIR 节点和生成输出具有公开可配置 `ResourceLimits`，逐阶段以 `MPF0010` 失败关闭并有 exhaustion tests
- [x] 增加 normalized HIR 与双目标 lowering golden、面向人的目标 semantic LIR dump；parser token/depth、AST arena、各 IR/产物/source-map exhaustion tests 已完成
- [x] 增加全管线 fuzz target、拒绝/成功 corpus、确定性 mutation smoke、libFuzzer crash replay 与最小化工作流
- [x] 全量源 runner/Node.js/生成 `cpp`/oracle 差分在新管线通过，诊断和输出变化均有审核记录
- [x] 增加小文件延迟、大文件吞吐、峰值 arena、深 CFG、大 shape、跨函数图、批量并发和生成产物 benchmark，并由 CI 执行 JSON 基线门禁
- [x] ASan/UBSan、clang-tidy、format、85% 覆盖率、CodeQL、Linux/macOS/Windows 与 GCC/Clang/AppleClang/MSVC 门禁全部接入；GitHub Actions 按快速反馈、兼容/差分、质量、Sanitizer、覆盖率、性能、安全和发布独立失败域
- [x] 删除共享 `Program` 直通 emitter 的生产路径；两个 emitter 只能接收对应 opaque LIR artifact
- [x] 删除 statement parser 的共享 syntax scratch、parser facade、整树 AST 转换器及只服务兼容树的 function-graph/code-binding 路径；架构门禁阻止恢复
- [x] 删除 MIR 递归 `Expression`/`Statement` 宽结构 ownership 投影；改为 `MirExpressionId`/`MirStatementId` 稠密 arena，双目标只按 ID 构建私有 LIR
- [x] 删除 flat MIR 节点中尚存的源语义 payload 镜像，以 revision-bound 强类型 operation attribute table 保存非结构事实，并把 lazy evaluation、load/allocate/store/copy/writeback 固化到 instruction/CFG

完成定义：P0—P7 全部完成，且两个 emitter 的公共入口只能接收各自 LIR，才可宣称“商业级多层 IR 与易扩展前后端”已交付。

### 0.3.6：Python 比较与成员关系语义（已发布）

- [x] 为 `is`/`is not`、`in`/`not in` 建立专用 token、复合操作符和 Python comparison precedence，修正前置 `not` 的绑定范围
- [x] 以强类型 `ComparisonOperator` 贯穿语言 AST、窄 HIR、MIR 和双目标 LIR；frontend AST v3 与各层 verifier 拒绝二义 payload、空操作符和错误 chain arity
- [x] 明确 `None`/布尔/数值/string/list/tuple 的 equality、identity 与 membership 支持边界；同类 sequence 递归相等，list/tuple 跨种类不相等
- [x] JavaScript 使用私有 `Symbol` tuple tag 区分 list/tuple，并以 runtime helper 保持递归 equality、sequence 引用 identity 与 string/list/tuple membership
- [x] `cpp` runtime 支持跨元素类型递归 equality 与 membership；LIR 以 reference-lambda 临时值固定二元比较左到右单次求值
- [x] `cpp` capability validator 对 value container 无法保持的 sequence identity 稳定产生 `MPF2044`，Analyzer 对不可移植 scalar identity/非法 membership 产生 `MPF2045`
- [x] 覆盖成功、拒绝、链式短路、递归容器、list/tuple 差异、布尔/数值等价、CPython/Node.js/严格生成 C++/oracle 差分与 fuzz seed
- [x] 双目标 LIR schema 升至 v10，并同步版本、支持矩阵、诊断、架构、测试统计、golden、性能基线与 changelog

### 0.3.7：语言专属 arena parser 所有权收敛（已发布）

- [x] Python statement parser 直接向 `python::ast` arena 驻留表达式、statement、body 和 root，只以 `AstNodeId` 连接节点
- [x] Matlab statement parser 直接构造 `matlab::ast` 函数、控制流、多输出、indexed assignment 与恢复节点
- [x] Fortran free/fixed-form 后续 parser 直接构造 `fortran::ast` procedure、声明、selection、循环和 writable target
- [x] 提供只共享 arena 生命周期机制、不共享语言语法节点的 `FrontendAstBuilder<LanguageTag>`
- [x] 三个 parser API 返回编译期互不兼容的语言 `ParseResult`，frontend session 不再执行整树复制
- [x] 删除共享 `compiler::Program`/`Statement`/`ParseResult`、`make_*_ast` 转换器和 parser facade
- [x] 删除只服务旧 syntax tree 的 function-graph 与 code-binding overload，并收窄 `mpf-core` 编译面
- [x] 增加 direct-builder 架构门禁、三语言错误恢复/可达性、session allocator ownership 和强类型结果测试

### 0.3.8：MIR 稠密 arena 与递归投影删除（已发布）

- [x] 新增 `MirExpressionId`/`MirStatementId`，MIR expression child、statement body/alternative 和 roots 只保存强类型 ID
- [x] expression arena 节点绑定 resident instruction、SSA value、type、shape、storage 和确定的 operand order
- [x] statement arena 节点绑定 operation instruction，顶层与嵌套 ownership 形成唯一可达图
- [x] verifier 逐项核对 expression/instruction contract，并拒绝非法 child、丢失 definition 与 metadata 分歧
- [x] verifier 拒绝 operation 非稠密、非法 root、重复 ownership、cycle 和 unreachable node
- [x] JavaScript/`cpp` lowering 从 flat MIR 做 O(1) lookup，再分别构造私有 LIR
- [x] `cpp` capability 从 MIR function/call graph 判断递归；binding validator 扫描唯一 expression inventory
- [x] MIR dump 升至 v2，架构门禁和 160 项测试固定 flat arena contract

### 0.3.9：MIR operation payload 与求值语义收敛（已发布）

- [x] 将 flat MIR 节点内仍镜像的 source-semantic payload 拆为 revision-bound 强类型 operation attribute table，并以 `TypeId`/`ShapeId`/`StorageId` 为唯一事实
- [x] 为 conditional、短路逻辑和 comparison chain 建立显式 lazy CFG、`truthiness`/`compare` 与 typed value/storage merge，保证 MIR 自身表达求值顺序
- [x] 将变量读取、未初始化声明、assignment/indexed assignment 和 writable call 的 load/allocate/store/copy/writeback 固化为 runtime-independent opcode contract
- [x] 双后端、binding/capability、alias/effect、MIR v3 dump、稠密 verifier、架构门禁及正负测试全部迁移到新合约

### 0.4.0：TypeScript 首个独立纵切面（已发布）

- [x] 建立 TypeScript 6 独立 descriptor、语言专属 arena AST、全源 statement lexer/parser 与 AST→HIR semantic seed 首个端到端纵切面
- [x] 接通 typed `let`/`const`、标量/typed array、函数/default/value return、`if`/`else`、`while`、严格比较、`console.log`、显式函数 export 与双目标执行
- [x] 用 semantic profile/side-table→MIR function→JavaScript LIR ABI 贯通显式 export policy；TypeScript 私有函数不再被 ESM 默认导出
- [x] 增加 TypeScript descriptor/arena/conformance、拒绝语义、Node.js/生成 C++/oracle 差分和 typed-array 执行门禁

### 0.4.1：TypeScript lexical scope 与 canonical for（已发布）

- [x] 以 `ScopeModel`/`NameScopeEdges` 为 function、statement、body、alternative 建立可验证的语言 profile 驱动 scope graph
- [x] TypeScript `let`/`const` 支持真实 block-local declaration、嵌套遮蔽、最近外层赋值、`for` initializer TDZ 和离开 scope 后的稳定拒绝
- [x] Analyzer 在 block/branch/loop 间隔离局部符号，并只合并外层 binding 的确定赋值状态
- [x] 双目标名称规划按 `SymbolId` 建立 identity inventory，保留可读 scope-local spelling 并拒绝冲突 symbol/spelling 合约
- [x] 双目标 LIR 升至 v12，为 statement/body/alternative 固化 lexical `ScopePlan`，renderer 只序列化计划声明
- [x] TypeScript 新增 canonical C-style `for`、四种 update 形式、induction scope、break/continue 和稳定拒绝诊断
- [x] MIR 为 `for` 建立 preheader/condition/body/update/exit CFG、显式 store、回边 storage merge 与 continue-to-update contract
- [x] TypeScript `number` 统一为实数逻辑类型；typed array 只接受可证明的整数常量索引，避免目标端静默截断
- [x] JavaScript runtime fragment discovery 按 profile/comparison 精确裁剪，原生 TypeScript strict scalar comparison 不再携带 Python helper
- [x] block scope/for 示例、fuzz corpus、四路差分、golden、架构和损坏输入门禁全部通过

### 0.4.2 及后续：官方 grammar 与对象语义继续扩展

- [ ] 按 Matlab/Python/Fortran/TypeScript 官方 grammar 选择下一批可独立验收的纵切面；每累计 8—20 条独立更新形成下一版本
- [ ] 继续完成动态 rank/extent、广播、精确 N 维 selector region overlap 和目标 typed-array/ownership 策略

## M0：工程与端到端基础（完成）

- [x] C17/C++17 标准配置、CMake 3.20+ 和严格根目录 `build/` 构建边界
- [x] 分层源码目录、公共 C++ API 与 `mpfc` CLI
- [x] 统一诊断结构、错误码、源码位置、文本/JSON 输出和退出码契约
- [x] Matlab/Python/Fortran 标量子集与 TypeScript 首个 typed 子集到 JavaScript/C++ 的端到端纵切面
- [x] JavaScript ESM/strict script 与 C++17 translation unit 输出
- [x] stdin/stdout、自动语言检测和 Fortran source-form 选择
- [x] Linux/macOS/Windows，GCC/Clang/AppleClang/MSVC CI
- [x] 标签构建、测试、安装、归档和 GitHub Release 流水线

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
- [x] 前端名称/别名/扩展名/内容探测/解析回调统一为版本化 `FrontendDescriptor`
- [x] 后端名称/别名/availability/binding/validator/emitter 统一为版本化 `BackendDescriptor`
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
- [ ] 动态 rank、广播、跨 view/storage 的精确 N 维 section overlap 与多 writable actual alias 证明
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

已交付：

- [x] script logical statements、`...`、多行矩阵、行/block comment 和顶层分隔符
- [x] 当前函数/分支/循环/赋值子集的 statement lexer/parser
- [x] 字符向量与转置在 statement token 层的上下文区分
- [x] colon 计数 `for`、正负 step、`while`、`elseif`、`break`/`continue`
- [x] 一维向量、二维矩阵、1-based 行列索引和列主序线性索引
- [x] `:`, `start:stop`, `start:step:stop` 的行/列/block/线性读取与写入
- [x] 二维 shape/bounds、section conformability 和标量扩展
- [x] 文件级 local function、前向/跨函数调用、多输出绑定和转发
- [x] Node.js、生成 C++ 与 oracle 差分框架

仍需建设：

- [ ] 完整 command/function 调用语法和转置/字符串歧义解析
- [ ] 元胞数组、struct、string、table、datetime 等核心类型
- [ ] 一般矩阵运算、隐式扩展、complex 与稀疏数组语义
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
- [ ] assumed-rank、assumed-size、动态 extent 组合和更精确的 N 维 storage/alias/overlap 规则
- [ ] DO CONCURRENT、SELECT TYPE/RANK、WHERE、FORALL 与更完整 I/O
- [ ] COMMON/EQUIVALENCE/SAVE 等历史 storage 语义
- [ ] ISO_C_BINDING、外部库、BLAS/LAPACK 调用和链接适配策略

## M5：TypeScript 6 前端

当前状态：0.4.1 已在首个独立纵切面上交付 lexical block scope、最近 binding 解析和 canonical C-style `for`。TypeScript 使用独立 descriptor、statement token stream、编译期专属 `typescript::ast` PMR arena、verifier 与 AST→HIR visitor，不复用 Python/JavaScript parser 或其他语言 artifact；manifest 声明 1.0—6.0，但这不表示完整 TypeScript 6 grammar 已完成。

- [x] 增加 `typescript`/`ts` 源语言身份、`.ts`/`.mts`/`.cts` 探测与独立 descriptor；不把 TypeScript 作为其他 parser 的模式分支
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
- [x] 后端 descriptor API version、canonical name/alias、代码绑定、validator 和 emitter 回调契约
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
- [ ] 完整 alias/overlap、bounds policy 和源语言对象生命周期模型
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
- [ ] 明确公共 C++ API/ABI 兼容策略、弃用周期和 LTS 分支
- [ ] 评估并设计稳定 C17 API/ABI；当前没有可承诺的 C 接口
- [ ] 崩溃去重服务；parser/resource exhaustion 防护、fuzz replay/minimize 和性能回归门禁已完成
- [ ] 可复现构建、签名制品、provenance、SBOM 和发布密钥流程
- [ ] 完整用户手册、API reference、迁移指南和版本化兼容性报告
- [ ] release candidate、跨平台兼容性审计和 1.0 发布检查表

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
