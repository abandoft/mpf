# MPF 持续建设路线图

本路线图同时记录 **0.33.0 已发布基线** 与当前 **0.34 开发分支** 的真实状态。历史交付细节见
[CHANGELOG.md](CHANGELOG.md)，当前可依赖的语言子集见
[docs/LANGUAGE_SUPPORT.md](docs/LANGUAGE_SUPPORT.md)。目标版本号表示语法/语义覆盖上限，不表示已经完整兼容 Matlab 2024、Python 3.14、Fortran 2023 或 TypeScript 6；TypeScript 前端当前尚未实现。

前后端商业级重构的权威设计见 [docs/COMPILER_PIPELINE.md](docs/COMPILER_PIPELINE.md)。路线图中的“多层 IR”只能指语言 AST、HIR、MIR、JavaScript LIR/`cpp` LIR 五层均拥有独立强类型数据模型、verifier 和实际生产调用路径；不能用共享结构体 alias、stage flag 或未被 pipeline 消费的空壳类型标记完成。

## 当前基线

| 项目 | 当前开发分支状态 |
|---|---|
| 实现与构建 | CMake 3.20+；配置固定 C17/C++17；当前生产源码和公共 API 使用 C++17，尚无稳定 C API |
| 输出目标 | 独立 JavaScript 与 `cpp` 后端；`cpp` 当前生成严格 C++17 translation unit |
| 前后端边界 | 生产驱动固定经过语言 AST artifact→HIR→MIR→目标私有 semantic plan/LIR→emitter；两个目标不读取彼此产物 |
| 扩展架构 | frontend descriptor API v4、backend descriptor API v3；AST verifier、标准版本范围/schema/determinism manifest、TargetProfile、稠密 legalization、opaque artifact 和前后端 conformance harness 已接入 |
| IR 架构 | 三种语言使用编译期互不兼容的 PMR arena AST；Analyzer 结果在边界抽取为 revision-checked 稠密 `SemanticTable`，MIR 只消费 side table；MIR 已有 block argument/edge actual、循环与选择 CFG、stride/view/lifetime/alias；目标 lowering 产出带 origin chunk 的最终 LIR，emitter 仅序列化 |
| Python 最新能力 | relational/equality 比较链、右结合条件表达式、短路/惰性/单次求值；基础参数关联和递归固定序列解包 |
| Fortran 最新能力 | integer/character/logical `SELECT CASE`、范围/default、重叠检查和任意分支确定赋值合流 |
| 工程门禁 | 143 项内部测试；48 个差分 case、134 条工具完整环境执行路径；58 项 CTest；fuzz smoke、可选 libFuzzer、性能发布阈值、阶段报告；本轮生产代码覆盖率 88.34%（13517/15301） |
| 发布状态 | 0.x；没有长期 API/ABI 或完整语言兼容承诺 |

## 本轮商业级收尾验收（完成）

- [x] 三语言生产 artifact 使用编译期互不兼容的 PMR arena AST、稠密 ID、verifier、确定性 dump 和 AST→HIR visitor
- [x] 当前支持的分支、循环、loop-else、`break`/`continue` 与 `SELECT CASE` 使用 MIR basic block、terminator、block argument/edge actual；shape stride、storage view/lifetime/intent 和 alias relation 可验证，并提供保守 `alias_between` 查询
- [x] JavaScript/`cpp` representation、ABI、type/shape、名称、runtime 与 binding 决策在目标 lowering/renderer 完成；emitter 只序列化最终 chunk
- [x] 公共 output bundle 提供代码、source map v3、确定性 dependency manifest 与逐阶段 `CompilationReport`；CLI 支持 `--source-map`
- [x] 三语言/双目标 fuzz smoke、Clang/libFuzzer、资源耗尽、确定性重放、崩溃复现与最小化工作流落地
- [x] 延迟、吞吐、深 CFG、大 shape、函数图、八路并发、峰值 arena 和产物大小纳入版本化 JSON 发布门禁与 CI 报告

这里的“完成”只指上述架构与工程闭环；各语言官方 grammar、完整对象模型、动态 rank/广播、精确 N 维 overlap/alias 和稳定插件 ABI 仍由后续条目跟踪。

## 下一交付目标

### 0.34：商业级前后端与五层编译器管线（进行中）

本里程碑优先于继续扩大语言覆盖。目标是让现有三前端和两个后端全部经过真实的 `语言 AST → HIR → MIR → 目标 LIR → Emitter` 生产路径，并删除共享 `Program` 直通 emitter 的旧路径。详细职责和禁止依赖见 [商业级编译器管线方案](docs/COMPILER_PIPELINE.md)。

#### P0：基线、指标与依赖规则

- [ ] 固化当前全部生成代码、诊断、差分输出、编译时间、峰值内存和产物大小基线
- [x] 增加生产 stage/include architecture test 与 javascript-only、cpp-only、core-only 链接/安装隔离，禁止 frontend→MIR/backend、公共 IR→backend 和 javascript↔`cpp` 反向依赖
- [x] 为重构设定 resource limit、确定性和性能回退阈值；CI 产出机器可读阶段指标与性能报告
- [x] 迁移以 feature-equivalent adapter 保持既有语言行为，现有单元、集成与差分 corpus 继续作为兼容门禁

#### P1：编译 session、ID、arena 与 pass 基础设施

- [x] 建立 thread-confined `CompilationSession` 基础，集中拥有 SourceManager、monotonic resource 和阶段节点指标
- [ ] 将 interner、诊断、配置和全部阶段 arena 所有权迁入 `CompilationSession`，并让 IR 容器实际使用 session resource
- [x] 增加强类型 `AstNodeId`、`HirNodeId`、`SymbolId`、`MirFunctionId`、`BlockId`、`InstructionId`、`ValueId`、`TypeId`、`ShapeId`、`StorageId`、`LirNodeId` 和 `RuntimeSymbolId`
- [ ] 节点采用阶段 arena/monotonic resource，分析采用按 ID 稠密 side table；禁止跨 arena 长期裸指针和递归 shared ownership
- [x] 建立 HIR/MIR/LIR 强类型 pass manager、`AnalysisManager`、revision、preserved-analysis 精确失效、逐 pass verifier 和耗时 instrumentation
- [x] 建立确定性的 HIR/MIR textual dump 与稳定 verifier 内部诊断
- [x] 增加确定性语言 AST dump、最终 LIR serialized-chunk inventory、阶段峰值 arena 内存统计及机器可读 `CompilationReport`

#### P2：语言专属 AST 与 AST→HIR

- [x] 建立 `python::ast`、`matlab::ast`、`fortran::ast` 独立 artifact root、稠密 `AstNodeId` inventory、source origin 和 descriptor verifier
- [x] 三种生产 artifact 使用编译期互不兼容的 `python::ast`/`matlab::ast`/`fortran::ast` 节点、PMR arena、稠密 ID 与 AST→HIR visitor；artifact 不再封装共享 syntax tree
- [x] descriptor parser 只向生产管线发布本语言 AST；comparison chain、多输出签名、source form/declaration attribute 等已支持表面信息随 AST 节点保留（底层 statement parser 的短生命周期共享 scratch 仍待 P7 删除）
- [x] 建立独立 HIR contract，承接函数、参数关联、多返回、assignment pattern、range、selection、slice/section 和 intrinsic identity
- [x] 以共享语义 profile 表达 truthiness、logical result、division、equality、layout 和 top-level storage；capability validator 不再按 `SourceLanguage` 分支
- [x] 三个 frontend descriptor 分别提供 AST verifier 与 AST→HIR lowering；统一 HIR verifier 在生产路径逐 pass 执行
- [ ] 增加跨语言等价语义的 normalized HIR golden，并继续收窄当前宽 HIR statement/expression 数据模型
- [ ] 删除共享 IR 中带语言名的字段；新语言不得通过新增 `python_*`/`matlab_*`/`fortran_*` flag 接入

#### P3：独立 MIR 与公共分析

- [x] 建立 `Program`/`Function`/`BasicBlock`、block argument identity、稠密 instruction table 和单 terminator CFG 基础
- [x] 当前支持的 if/loop/loop-else/`break`/`continue`/`SELECT CASE` lowering 为真实 basic block、edge actual 与 block argument/phi equivalent
- [x] 建立当前 scalar/container 的 interned logical `TypeId` 稠密表
- [ ] 扩展 tuple、function/reference 类型签名及跨函数 type verifier
- [x] 建立独立 `ShapeId`，表达当前 rank、静态/动态 extent 与 layout
- [x] 增加 row/column-major canonical stride、dynamic-rank 标记、section view storage 与 shape canonicalization
- [x] 建立 `StorageId` 和 `no_alias`/`may_alias`/`must_alias` 基础模型
- [ ] 显式建模 view、copy-in/copy-out、writable actual、overlap 与 storage lifetime
- [x] 建立结构化 `EffectSet`：read、write、allocate、io、may-fail、control、external-unknown
- [ ] HIR→MIR 显式固定 evaluation order、短路、循环/选择 CFG、多结果、load/store 和 runtime-independent semantic operation
- [x] MIR verifier 检查稠密表、函数/块/指令唯一所有权、函数内 edge、terminator arity、值唯一定义及 definition-dominates-use
- [x] verifier 补齐 block argument/edge actual arity、定义顺序与 dominance、type/shape/storage metadata、view/lifetime/intent、alias relation 相容性；更完整的跨函数 call/return 与 memory-effect 证明随类型系统继续扩展
- [x] 将 Analyzer 当前全部节点输出（含 call association 与递归 assignment-pattern 路径）抽取到按 `HirNodeId` 稠密索引、带 HIR revision 的 `SemanticTable`；抽取后 HIR 语义字段为空，MIR 拒绝缺失/陈旧表且不再读取 HIR 语义投影
- [ ] 将 Analyzer 内部兼容计算从“单遍临时注解后 move-extract”改为直接 side-table accessor，并将 name/scope、flow、alias/effect 拆成独立可缓存 analysis pass；随后删除 HIR 中的兼容语义字段
- [ ] 首批默认优化只包括经证明安全的 CFG cleanup、constant folding、dead-pure elimination、copy propagation 和 shape canonicalization

#### P4：JavaScript LIR 与纯 emitter

- [x] 建立 JavaScript `TargetProfile`、MIR capability 和在生产 lowering 中逐 instruction 执行的稠密 legalization registry
- [x] 建立不借用 MIR 生命周期的 JavaScript semantic lowering plan，提前解析 intrinsic binding、runtime feature、dependency 和源语义 profile
- [ ] 把 JS 数据表示、calling convention、module/chunk 与 NPM/runtime manifest 完整固化到 semantic IR
- [ ] 建立 representation lowering 和独立 JavaScript AST/LIR，表达 module/script、import/export、表达式/语句、临时值、IIFE/closure 和 source-map segment
- [x] 建立 JavaScript target pass pipeline，完成保留字安全名称分配、dependency canonicalization 和确定性排序
- [ ] 将临时值/声明 canonicalization 和全部 representation choice 从 emitter 前移到 LIR pass
- [x] 把 JavaScript emitter 中的源语言分支、runtime 扫描和 binding lookup 迁入 MIR→LIR lowering
- [x] 为 legalization、semantic lowering plan 和 AST/LIR 建立 verifier，emitter 公共入口只能接收 JavaScript LIR artifact
- [x] representation/type/shape/runtime/name 选择全部在目标 lowering/renderer 完成；emitter 仅 `serialize_chunks`，公共结果输出 source map v3 与确定性 dependency manifest

#### P5：`cpp` LIR 与纯 emitter

- [x] 建立 `cpp` `TargetProfile`、MIR capability 和在生产 lowering 中逐 instruction 执行的稠密 legalization registry
- [x] 建立不借用 MIR 生命周期的 `cpp` semantic lowering plan，提前解析 intrinsic binding、runtime feature、dependency 和源语义 profile
- [ ] 把具体类型、calling convention、value/reference/ownership 与 ABI 完整固化到 semantic IR
- [ ] 建立 representation/ABI lowering 和独立 `cpp` AST/LIR，表达 translation unit、include、namespace、声明/定义顺序、template、临时/lambda、copy/move 和 runtime ABI 调用
- [x] 建立 `cpp` target pass pipeline，完成保留字安全名称分配、函数依赖排序、dependency canonicalization 和确定性排序
- [ ] 将 temporary/declaration canonicalization 和全部 representation/ABI choice 从 emitter 前移到 LIR pass
- [x] 把 `cpp` emitter 中的源语言分支、函数排序、target validation、runtime 扫描和 binding lookup 迁入 MIR→LIR lowering
- [x] 为 legalization、semantic lowering plan 和 AST/LIR 建立 verifier，emitter 公共入口只能接收 `cpp` LIR artifact
- [x] representation/ABI/type/shape/name 选择全部在目标 lowering/renderer 完成；emitter 仅 `serialize_chunks`，公共结果输出 source mapping 与 dependency manifest
- [x] JavaScript 和 `cpp` LIR/CMake target/header 互不依赖，任一目标关闭时另一目标和 core 可独立构建、安装和消费

#### P6：descriptor、扩展 SDK 与门禁

- [x] Frontend descriptor API v4 提供 language AST verifier、AST→HIR factory、可验证 minimum/maximum language version、AST schema 与 determinism/reentrancy manifest；公共 API/CLI 支持 `LanguageVersion`/`--language-version`
- [ ] 增加显式 parser session factory、feature bitset 与 resource-limit contract
- [x] Backend descriptor API v3 提供 TargetProfile、legalization、capability、semantic IR/LIR factory、target verifier/printer 与 artifact schema manifest
- [ ] 增加完整 configuration schema 与未来外部 runtime 的 license/supply-chain manifest；code/source-map/dependency output bundle contract 已落地
- [x] 建立 Backend SDK 基础：opaque artifact lifecycle、强类型 target pass、legalization registry、binding、origin、确定性名称和 conformance 工具
- [x] descriptor catalog validation 覆盖 API version、线程安全/确定性声明、profile/target 一致性、legalization 完整性和 callback 完整性；内置 catalog 保持静态只读
- [x] 提供可复用 frontend/backend conformance harness，自动验证 descriptor、verifier、binding、lowering 和逐字节确定性
- [ ] 增加仓库外最小 frontend/backend 示例及安装后 conformance 模板
- [ ] 明确内部 C++ descriptor 仍不是动态插件 ABI；稳定 C17 plugin ABI 另立里程碑

#### P7：测试、性能与删除兼容路径

- [x] HIR/MIR/双目标 LIR 增加 verifier negative tests；HIR/MIR 增加 deterministic textual dump；后端 conformance 逐字节验证 emitter 确定性
- [x] source、AST/HIR/MIR/LIR 节点和生成输出具有公开可配置 `ResourceLimits`，逐阶段以 `MPF0010` 失败关闭并有 exhaustion tests
- [ ] 补齐各层 lowering golden 与面向人的目标 semantic LIR dump；parser token/depth、AST arena、各 IR/产物/source-map exhaustion tests 已完成
- [x] 增加全管线 fuzz target、拒绝/成功 corpus、确定性 mutation smoke、libFuzzer crash replay 与最小化工作流
- [x] 全量源 runner/Node.js/生成 `cpp`/oracle 差分在新管线通过，诊断和输出变化均有审核记录
- [x] 增加小文件延迟、大文件吞吐、峰值 arena、深 CFG、大 shape、跨函数图、批量并发和生成产物 benchmark，并由 CI 执行 JSON 基线门禁
- [x] ASan/UBSan、clang-tidy、format、85% 覆盖率、CodeQL、Linux/macOS/Windows 与 GCC/Clang/AppleClang/MSVC 门禁全部接入；GitHub Actions 按快速反馈、兼容/差分、质量、Sanitizer、覆盖率、性能、安全和发布独立失败域
- [x] 删除共享 `Program` 直通 emitter 的生产路径；两个 emitter 只能接收对应 opaque LIR artifact
- [ ] 删除 statement parser 的共享 scratch、HIR/MIR 宽结构兼容投影；emitter 内 lowering 已删除，文档、目录和 CMake export 已同步

完成定义：P0—P7 全部完成，且两个 emitter 的公共入口只能接收各自 LIR，才可宣称“商业级多层 IR 与易扩展前后端”已交付。

### 0.35：Python 比较与成员关系语义

- [ ] 为 `is`/`is not`、`in`/`not in` 建立专用 token、AST/IR 和优先级规则
- [ ] 明确 `None`/布尔/数值/string/list/tuple 的 equality、identity 与 membership 支持边界
- [ ] JavaScript runtime 区分 list/tuple 等不同 sequence kind，避免跨类型 equality 被 Array 表示抹平
- [ ] C++ capability validator 对无法静态保持的动态对象比较稳定失败关闭
- [ ] 覆盖成功、拒绝、短路/单次求值、CPython/Node.js/生成 C++/oracle 差分
- [ ] 同步版本、支持矩阵、诊断、测试统计与 changelog

## M0：工程与端到端基础（完成）

- [x] C17/C++17 标准配置、CMake 3.20+ 和严格根目录 `build/` 构建边界
- [x] 分层源码目录、公共 C++ API 与 `mpfc` CLI
- [x] 统一诊断结构、错误码、源码位置、文本/JSON 输出和退出码契约
- [x] Matlab/Python/Fortran 标量子集到 JavaScript/C++ 的端到端纵切面
- [x] JavaScript ESM/strict script 与 C++17 translation unit 输出
- [x] stdin/stdout、自动语言检测和 Fortran source-form 选择
- [x] Linux/macOS/Windows，GCC/Clang/AppleClang/MSVC CI
- [x] 标签构建、测试、安装、归档和 GitHub Release 流水线

## M1：编译器基础设施

已交付：

- [x] 多文件 SourceManager、源码所有权、UTF-8 列、CRLF、跨度、行映射和诊断片段
- [x] 三语言 logical-source normalization、表达式 lexer 和 statement token/span stream
- [x] 三语言当前子集的递归下降 statement parser；已支持语法不再使用 regex/prefix 行解析
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

- [ ] 依据三种官方 grammar 完成 statement/parser 覆盖；版本范围 contract、公共 API/CLI gate、Python 3.8 positional-only 与 Fortran 2003 bracket-constructor gate 已落地，其余产生式/feature gate 继续逐项完成
- [x] 建立独立 HIR/MIR、强类型 ID、verifier、pass/analysis manager、确定性 dump 和生产 lowering 主链路
- [ ] 语言专属 arena AST、当前控制结构 MIR CFG/alias、parser token/depth/arena 边界和 Analyzer 输出 side table 已完成；仍需删除 Analyzer 内部临时 HIR 注解并继续扩展 parser recovery
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
- [x] bool/number equality 和当前同类递归 list equality 的 JavaScript runtime
- [x] 固定 tuple/list 解包、递归 assignment pattern、每层单 starred target 和跨函数结构元数据
- [x] `for ... in range`、正负 step、`while`、`elif`、loop-else、`break`/`continue`
- [x] logical line、括号/反斜杠续行、注释、tab 展开缩进和顶层分号 simple statements
- [x] 基础 list、矩形嵌套 list、负索引、链式索引、`len`、`sum`
- [x] `start:stop:step` 读取；普通切片 resize、extended slice 等长写入和运行时检查
- [x] `float` 的数字/布尔/字符串基础转换以及 NaN/Infinity truthiness
- [x] CPython 3.14、Node.js、生成 C++ 与 oracle 差分门禁

仍需建设：

- [ ] 以 Python 3.14 grammar 建立 PEG/等价完整 parser 与旧版本语法门控
- [ ] `is`/`is not`、`in`/`not in`，以及 list/tuple 跨类型 equality/identity 的精确规则
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

当前状态：目标已纳入产品范围，尚未宣称任何 TypeScript 语法子集可用。frontend descriptor v4、语言专属 arena AST、AST verifier 与 AST→HIR contract 已形成接入骨架；TypeScript 前端必须新增自己的节点类型、verifier 与 visitor，不得复用现有 parser scratch 或其他语言 artifact。

- [ ] 增加 `typescript` 源语言身份、`.ts`/`.tsx` 探测与独立 descriptor；不得把 TypeScript 作为 Python/JavaScript parser 的模式分支
- [ ] 以 TypeScript 6 grammar 建立 lexer/parser、版本门控和稳定诊断
- [ ] 明确 type erasure、enum、namespace、decorator、JSX/TSX 和 module lowering 边界
- [ ] 将标准库与宿主 API 映射为 source intrinsic/外部 binding，不在 emitter 中匹配源 spelling
- [ ] JavaScript 输出保持 TypeScript 运行时语义；`cpp` 对动态对象模型建立明确 capability validator
- [ ] 增加 tsc/Node.js/生成 C++/oracle 差分和 TypeScript-only 自动检测/拒绝 corpus
- [ ] 更新 CLI、支持矩阵、安装包、示例和兼容性报告

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
- [x] Python truthiness、and/or、比较链、条件表达式和当前 equality runtime
- [x] Node.js 执行、生成 C++ 严格编译执行和单一差分 manifest

仍需建设：

- [x] 建立独立 JavaScript LIR/`cpp` LIR、TargetProfile、稠密 legalization、私有 semantic plan、target pass/verifier 和 opaque artifact 入口
- [x] 将两个 emitter 内 representation/ABI/type/shape/name/runtime 决策前移到目标 lowering/renderer；emitter 成为纯 serialized-chunk 序列化器
- [ ] 精确整数/浮点/complex、typed-array 布局、广播和一般 N 维数组策略
- [ ] 完整 alias/overlap、bounds policy 和源语言对象生命周期模型
- [ ] JavaScript ESM chunking、tree shaking、稳定 name mangling 和 `.d.ts` 输出
- [ ] NPM runtime 包、semver、锁文件、SBOM、许可证审计和浏览器/Node conformance matrix
- [ ] C++ runtime 从生成 translation unit 拆分、头文件/实现布局、可配置 namespace 和 ABI 策略
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
