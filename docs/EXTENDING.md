# 扩展前端、后端与代码绑定

0.3.4 使用对称的 descriptor/registry 架构接入内置源语言和输出目标。核心驱动执行“选择 descriptor → 创建 parser session → parse 语言 AST → AST verifier → AST→HIR → HIR pass → MIR → MIR pass → capability/legalization → 私有 semantic plan/LIR → LIR verifier/dump → printer”，不按具体语言或目标硬编码分派。当前 contract 面向同一源码树中的编译期组件；descriptor 带 API version，但尚不承诺跨动态库的稳定插件 ABI。

本页记录当前可执行的 frontend API v5/backend API v5 接入方式以及尚未完成的动态插件 contract。语言 AST artifact、Analyzer 直写 semantic side table、独立 name/scope、flow 与 MIR alias/effect side table、call argument borrow/copy/optional-forward contract、当前控制结构 MIR CFG、目标 lowering 和纯 serialized-chunk emitter 已实际进入生产路径；statement parser 的共享 scratch、HIR/MIR 宽兼容投影与精确 N 维 selector region overlap 不是新扩展接口。权威边界见 [商业级编译器管线方案](COMPILER_PIPELINE.md)。

## 设计约束

- descriptor 数据使用静态存储期和 `std::string_view`，注册表不取得所有权，也不在热路径分配。
- 注册顺序不能改变检测结果；扩展名必须唯一，内容探测最高分唯一，否则自动检测失败关闭。
- 不使用全局构造器自注册，避免链接器裁剪、初始化顺序和多动态库状态问题。
- 前端只能生成自己的语言 AST artifact 和诊断，不能调用 MIR/backend/emitter 或拼接目标代码；接入核心只能通过 AST verifier 与 AST→HIR lowering。
- Analyzer 只解析源语言语义，不选择目标 lowering。
- 每个后端直接读取同一份已分析 IR，不读取其他后端的输出。
- 所有未知语法、不可保持的语义和缺失绑定必须在生成前产生稳定诊断。

## 接入新的源语言

一个内置前端由 `FrontendDescriptor` 描述：

```text
language identity
canonical name + aliases
filename extensions
allocation-free content probe
language version + feature bitset + resource contract + AST schema manifest
source-specific intrinsic binding table
parser-session factory
language AST verifier
AST-to-HIR lowering callback
```

接入步骤：

1. 在公共 `SourceLanguage` 中加入稳定身份；名称使用语言名，不携带实现标准版本号。
2. 在独立目录/源文件中实现 logical-source normalization、statement lexer/parser 和语言专属 PMR arena AST；节点类型必须与其他语言编译期不兼容，不能把共享 syntax `Program` 放进 artifact。
3. 提供 descriptor factory 和独立 parser-session factory；扩展名和探测规则归前端所有，解析选项、arena、资源上限和请求 feature 通过 `FrontendParseOptions` 传入，并声明版本范围、能力与 AST schema。
4. 在 `frontend_registry.cpp` 的静态 catalog 增加一项，并将组件源码加入 `mpf-core`。
5. 由 descriptor 显式选择一组有序 spelling → `IntrinsicId` 表；只有源语言确实提供相同全局拼写时才选择共享数学表，TypeScript 一类语言可以完全不选。语义相同的函数复用已有 ID，语义不同的函数必须新增 ID。
6. 提供 AST verifier 和 AST→HIR lowering；运行 `run_frontend_conformance` 验证 descriptor、重复 parse/lowering 确定性和 HIR contract。
7. 增加别名/扩展名冲突、探测优先级、parser 成功/拒绝、双后端行为和差分测试。
8. 更新语言支持矩阵、诊断索引和版本目标；CLI 帮助会从 registry 自动枚举 canonical name。

HIR 是当前前端扩展边界。新语言特性若不能由 HIR 无损表达，应先扩展 HIR/MIR semantic contract 和两个后端的 capability contract，不得借用字符串标记或语言名布尔字段绕过语义层。新表面语法必须留在语言 AST；跨语言规范语义才进入 HIR。

## 接入新的输出目标

一个内置后端由 `BackendDescriptor` 描述：

```text
target identity
canonical name + aliases
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
2. 新建独立 CMake target、descriptor、TargetProfile、稠密 legalization table、binding table、capability、semantic plan、LIR/pass/verifier 和 emitter；只依赖 `mpf-core`/允许的 backend-common 设施。
3. 在 backend registry 增加目标 metadata 与条件 factory，在 facade 中加入对应构建选项和链接边界。
4. 为每个 `IntrinsicId` 提供显式 binding；不支持的项保留 `unavailable`，让 `MPF0004` 在 emitter 前失败关闭。
5. 运行 `run_backend_conformance`，以同一 MIR 和 revision-checked alias/effect table 验证 descriptor/profile/legalization/binding、重复 lowering、artifact verifier 和逐字节确定性。
6. 增加 target-only、其他后端禁用和 core-only 的全新构建/安装/外部消费测试，确认 compilation database 不包含禁用目标源码。
7. 增加生成代码的语法、编译或执行门禁，并记录 runtime、依赖、许可证和供应链策略。

后端不得把另一个目标当作中间语言。目标间可以共享经过审计的名称改写、IR visitor 或算法，但不能共享生成产物或目标语法字符串。

## 当前生产生命周期与最终收敛目标

frontend descriptor 当前已经提供以下生命周期：

```text
create frontend session
  → normalize/lex/parse language-owned AST
  → verify AST
  → lower AST to HIR
  → destroy frontend session/AST arena when no longer needed
```

当前内置前端由 `CompilationSession` 提供 SourceManager/arena/资源上限，每次 parse 经 descriptor factory 创建独立 parser session；feature 请求不是已声明能力的子集时失败关闭。

新前端只能依赖 source、syntax-common 和 HIR contract。它负责版本范围、feature manifest、AST verifier 和 AST→HIR conformance；不能 include MIR pass、目标 LIR 或 backend 头文件。

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

当前 descriptor 已覆盖 configuration schema、runtime component/license/origin/integrity 供应链清单、semantic LIR dump 和 code emitter；公共 `TranspileResult` 提供 code、source map v3、确定性 dependency manifest 与阶段报告。项目自身尚未选择 SPDX 许可证，因此内联 runtime 如实使用 `LicenseRef-MPF-Project`，不能由接入方擅自改写为 MIT/Apache。

新后端只依赖 MIR/analysis/pass contract 和允许的 backend-common 设施。capability 与 lowering 必须拒绝 revision、inventory 或 fixed-point verifier 不一致的 alias/effect facts；不得绕过该输入重新使用已删除的 MIR 内嵌 effect。MIR `CallArgument` 已确定 value/borrow/copy/optional-forward/omitted 语义，新后端必须将 transfer plan 复制到自己的 semantic IR/LIR，再选择目标 ABI；不得在 printer 中重新根据 expression/section 形状推断 copy-out。函数 parameter passing/module/export/recursive declaration 和所有 renderer 所需临时值也必须在 target LIR 中显式规划并验证；推荐使用按 `LirNodeId` 的稠密 CSR inventory，printer 不得持有可变名称分配器。每个后端拥有自己的 LIR 类型，不能把通用目标结构体加若干 target flag 当作 LIR，也不能调用其他后端的 capability、binding、lowering 或 emitter。

目标 descriptor contract 至少包括：

- stable identity、canonical name、alias 和 configuration schema；
- 支持的 MIR feature/capability manifest；
- TargetProfile、MIR capability 和 legalization-registry factory；
- target semantic IR、representation/ABI lowering 和 target AST/LIR factory；
- target pass pipeline、LIR verifier 和 printer factory；
- runtime dependency、license 和供应链 manifest；
- descriptor API version、线程安全和确定性声明。

descriptor API 升级时必须保留 catalog validation 和禁用组件 metadata 查询，但不要求为内部 0.x C++ 布局提供 ABI 兼容。动态插件需等待稳定 C17 ABI、allocator/ownership、version negotiation、错误边界、签名和隔离方案单独完成。

## Extension conformance harness

仓库已提供 `run_frontend_conformance` 与 `run_backend_conformance`：前者验证 descriptor、parser session、AST verifier 和重复 HIR dump，后者验证 MIR、alias/effect table、binding、capability、lowering、artifact verifier、semantic dump 和重复输出。resource limit、source map、fuzz/performance 与安装隔离由全局 harness 补充；`examples/installed/frontend` 和 `examples/installed/backend` 会在 staging install 后作为真正独立的 CMake consumer 配置、构建和运行。

前端 conformance 至少验证：

- descriptor/alias/extension/probe/version manifest；
- AST ID/span/ownership 和 verifier negative case；
- AST→HIR normalized golden、确定性和 resource limit；
- 禁止依赖 MIR、LIR 或 backend target；
- 至少一个成功、一个边界和一个拒绝端到端用例。

后端 conformance 至少验证：

- descriptor/configuration/capability manifest；
- 全量 intrinsic binding 的 direct/custom/unavailable 明确选择；
- capability/legalization 完整性、MIR→target semantic IR→LIR golden、逐层 verifier negative case 和缺失 binding 失败关闭；
- call argument transfer/lifetime、writable overlap 失败关闭，目标 LIR 不重新携带源 call intent 并实际消费 transfer plan，以及 ABI/temporary inventory 的 dense、arity、碰撞和缺失负向测试；
- emitter 确定性、语法/编译、source map 与 dependency manifest；
- target-only、其他后端关闭、core-only 构建/安装/外部消费隔离。

一个新输出语言的推荐目录固定为：

```text
backends/<target>/
├── descriptor + target_profile
├── capability
├── legalization
├── type_and_representation_lowering
├── intrinsic_and_runtime_bindings
├── semantic_ir + verifier
├── lir + passes + verifier
└── emitter_or_printer
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
- language/target identity、canonical name、alias 和 extension 冲突；
- 大小写不敏感名称查询与禁用后端 metadata；
- 内容探测的唯一最高分和歧义失败关闭；
- 全量 intrinsic 的目标绑定完整性；
- 缺失 binding 不进入 emitter；
- 新组件关闭时不编译、不链接、不安装其实现源码。

大规模 grammar 的性能由 lexer/parser arena、增量源码视图、resource limits 和 benchmark corpus 约束；新组件必须加入相应 fuzz seed 与性能场景。registry 本身保持无锁只读、无堆分配。若未来提供运行时插件，将另行设计稳定 C ABI、所有权、线程安全、版本协商、签名与隔离模型，不能直接把当前内部 C++ descriptor 暴露为插件 ABI。
