# MPF 架构

本文描述当前 0.34 开发分支的实际架构；0.33.0 是最新发布基线。CMake 同时固定 C17/C++17 标准基线，但当前生产实现和公共 API 使用 C++17；`cpp` 是 C++ 输出目标的代码身份，C++17 是当前生成标准。最终职责、性能模型和迁移验收条件见 [商业级编译器管线方案](COMPILER_PIPELINE.md)。

## 当前状态边界

生产驱动已经切换为五层路径，旧的共享 `Program`→emitter 直通入口不存在：

- 三个 frontend descriptor 产生编译期互不兼容的语言 AST artifact；节点以稠密 `AstNodeId` 存入 session PMR arena，并显式运行 AST verifier 与 AST→HIR visitor；
- Analyzer 和 HIR pass 只处理 HIR；HIR→MIR 生成稠密 type、shape、storage、instruction、function/basic-block 表和 effect；
- MIR lowering 为当前 if/loop/loop-else/`break`/`continue`/`SELECT CASE` 建立真实 CFG、block argument 和 edge actual，并显式记录 shape stride、storage view/lifetime/intent 与 alias relation；
- MIR verifier 检查表密度、唯一所有权、terminator/edge arity、定义顺序、dominance、type/shape/storage metadata、view/lifetime/intent 与 alias relation；
- JavaScript 与 `cpp` 分别执行 TargetProfile、逐 opcode legalization、capability、私有 semantic plan、独立 LIR/pass/verifier；
- representation/type/shape/ABI/name/runtime 决策在目标 lowering/renderer 完成，形成带 source origin 的 serialized chunks；核心只持有 opaque target artifact，两个 emitter 只调用 `serialize_chunks`。
- facade 从最终 LIR origin 构建 source map v3，并公开 dependency manifest 和包含阶段耗时/节点/峰值 arena 的编译报告。

本轮商业级收尾已完成语言 AST artifact、当前支持语义的 CFG/alias、纯 emitter、source map、资源防护、fuzz 和性能发布门禁。0.34 仍有更大范围的迁移任务：statement parser 内还有不会越过 frontend descriptor 的短生命周期共享 scratch；Analyzer 仍把部分结果写回宽 HIR；MIR 为兼容当前 target lowering 保留结构化语义投影；完整官方 grammar、一般 N 维 storage/alias、跨函数 call/return 类型系统和稳定插件 ABI 也尚未完成。所有边界逐项记录在 [TODO 0.34/P0—P7](../TODO.md)。

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
      └─► Fortran fixed/free source-form rules
      │
      ▼
statement syntax layer
      ├─► Python statement lexer ─► recursive parser
      ├─► Matlab statement lexer ─► recursive parser
      └─► Fortran statement lexer ─► recursive parser
                                  └────────► diagnostics
      │
      ▼
FrontendDescriptor.verify → FrontendDescriptor.lower
      │
      ▼
HIR verifier/pass → target-neutral binding + definite assignment + type/flow analysis
source intrinsic spelling ─► stable IntrinsicId
      │
      ▼
HIR→MIR → MIR verifier/pass
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
  → MIR：类型化 CFG、值、shape、storage、alias 与 effect
  → JavaScript LIR / cpp LIR：目标专属 capability、binding 与 lowering
  → emitter：确定性序列化
```

五层已有不同强类型表示和 verifier。生产 AST 不包含共享 syntax tree，最终 emitter 不包含 lowering；当前保留的宽 HIR/MIR 语义投影是后续压缩数据模型与迁移 side table 的兼容层，不构成跨后端生成依赖。详细 contract 和禁止依赖见 [COMPILER_PIPELINE.md](COMPILER_PIPELINE.md)。

0.8 将目标无关编译和目标能力验证彻底分层；0.9—0.23 逐步建立 section、SourceManager、差分、tokenized parser、多输出、函数图、Fortran procedure、引用与 section copy-in/copy-out；0.24/0.25 建立 Fortran argument association 与完整当前 optional intent；0.26 将通用 call keyword/default 元数据复用于 Python positional-only/keyword-only association；0.27 将同一多目标赋值 IR 扩展到 Python 固定序列解包；0.28 将 C++ 目标身份统一为 `cpp`；0.29 抽取递归 `AssignmentPattern` 与 `ValueMetadata`；0.30 建立格式、静态分析、覆盖率和安全扫描门禁；0.31 新增结构化 SELECT/CASE IR、任意分支合流和双后端单次求值 lowering；0.32 新增显式 Python comparison-chain/conditional AST、类型关联验证和双后端惰性单次求值 lowering；0.33 建立对称的前后端 descriptor/registry、稳定 intrinsic ID 与目标代码绑定表。Analyzer 对不同源语言分别确认 association、unpacking、selection 与表达式语义，后端不解析源语言参数或控制结构语法。通用 Analyzer 不接收 `TargetLanguage`，任一目标均不依赖另一目标的生成物。

0.32 的 Python comparison-chain 节点直接保存操作数序列和操作符序列，conditional 节点保存 condition/true/false 三个子节点。Analyzer 分析这些节点的 type/element/shape/tuple metadata，但不选择目标 lowering；JavaScript IIFE 与 C++ lambda 分别由自己的 target renderer materialize，最终 emitter 不再处理这些语义。

0.34 开发分支把 descriptor 升级到 API v3。前端 manifest 声明语言版本范围、AST schema、确定性和 reentrancy，并提供 AST verifier/lowering；后端 manifest 声明目标标准、artifact schema、TargetProfile 和 legalization factory。catalog 保持静态只读、无全局构造器自注册，conformance harness 会重复 lowering/emit 并比较确定性。当前 contract 用于编译期内置组件，不是稳定动态插件 ABI；接入步骤见 [扩展指南](EXTENDING.md)。

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
- `lexer`/`lexers`：公共表达式 token/scanner、语言独立词法规则、共享 `BasicStatementToken` 载体，以及带 byte span/源位置的三语言 statement lexer。
- `compiler`：statement parser 的短生命周期 scratch、递归 assignment pattern/value metadata、Pratt parser、函数依赖图、稳定 intrinsic ID 和代码绑定验证；共享 scratch 不进入 frontend artifact 或 AST→HIR 之后的生产层。
- `ir`：强类型 ID、semantic profile、HIR、MIR、pass/analysis manager、verifier 与确定性 dump。
- `semantic`：目标无关的作用域、名称绑定、builtin 遮蔽、确定赋值、循环上下文、不可达代码、表达式分支类型、标量/元素类型、矩形 shape、动态 extent、slice 长度、section conformability、逐维静态越界和 rank。
- `frontends`：统一 descriptor/registry，以及每种语言独立 logical-source normalizer 与递归下降 statement parser；parser 只消费 statement token/span，并把表达式跨度交给 Pratt parser。当前迁移覆盖已支持子集，完整官方 grammar 仍按各语言里程碑扩展。
- `backends`：descriptor v3、TargetProfile/legalization、目标 intrinsic binding、独立 capability/semantic plan/LIR/pass/verifier、target renderer 与纯 serialized-chunk emitter，以及仅供后端使用的保留字/冲突安全名称计划。Python loop-else 使用每层独立完成标志；数组访问通过带 shape/bounds/base/negative-index/column-major 策略的 runtime lowering。生成 C++ 放入 `mpf_generated` namespace，runtime 放入独立 `mpf_runtime` namespace。
- `cli`：文件和参数 I/O，不包含编译语义。

## 正确性策略

每一项语言能力至少包含：成功样例、边界样例、拒绝样例，以及两个目标后端的行为测试。可执行成功样例进入单一 corpus manifest；JavaScript 使用 Node.js 做语法/执行验证，C++17 生成物使用顶层相同 compiler/generator 严格编译执行，源 runner 可用时直接参与同一结果比较。诊断码、JSON schema 与 CLI 退出状态是工具集成契约，删除或重定义需记录在变更日志；详见 [测试文档](TESTING.md) 与 [诊断文档](DIAGNOSTICS.md)。

## 依赖策略

核心库当前不依赖第三方组件。未来引入解析器生成器或 NPM/runtime 组件前，需记录版本、许可证、供应链固定方式和跨平台影响。标准数值函数分别映射到 ECMAScript `Math` 与 C++ `<cmath>`；仅当目标语言缺少等价语义时才引入小型、可审计的 runtime。
