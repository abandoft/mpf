# Matlab → JavaScript 产品计划

本文是 `matlab2js` 的专项产品路线图和验收清单。通用编译器边界以
[COMPILER_PIPELINE.md](COMPILER_PIPELINE.md) 为准，已经发布的准确能力以
[LANGUAGE_SUPPORT.md](LANGUAGE_SUPPORT.md) 为准，跨项目优先级仍由根目录
[TODO.md](../TODO.md) 管理。

## 当前结论

当前实现是一个架构完整、可端到端执行的 **Matlab 基础子集**，不是完整或接近完整的
Matlab 2024 转译器。它已经证明语言专属 arena AST、HIR、MIR、JavaScript LIR、纯
renderer、source map 和差分框架能够工作，但还不能安全承载一般生产 Matlab 工程。

| 维度 | 当前状态 | 商用阻断项 |
|---|---|---|
| 源码与语句 | logical statement、注释、续行、脚本、local function、分支、循环和标量 `switch` | 完整 command syntax、`try/catch`、workspace 声明、包/类、产生式级 recovery |
| 表达式 | 标量算术/比较、调用、向量/矩阵字面量、矩阵/逐元素算术 token 身份、基础 colon/index/section | 转置、`end`、逻辑索引、矩阵除法/幂、短路/逐元素逻辑完整语义 |
| 数据模型 | number、boolean、字符文本的当前子集、矩形嵌套数组 | numeric class、complex、sparse、string、cell、struct、table、datetime、对象 |
| 数组语义 | 1-based、列主序线性索引、静态 N 维 reshape/section、同 shape/标量逐元素算术、二维矩阵乘法 | 一般 N 维动态 shape/隐式扩展、矩阵除法/幂、删除、growth、重复/逻辑索引、值语义和 alias 契约 |
| 函数 | 文件级 local function、前向调用、单/多输出 | `nargin`/`nargout`、`varargin`/`varargout`、function handle、anonymous/nested closure、workspace |
| JavaScript runtime | 内嵌数组、索引、section、reshape 和基础 intrinsic runtime | 有版本的 Matlab runtime 包、NDArray 表示、数值/异常兼容层、依赖与许可证审计 |
| 验证 | Node.js、生成 C++、oracle、source map、fuzz smoke、性能框架 | 授权 Matlab reference runner；真实项目 corpus；语义、内存和性能发布阈值 |

矩阵 `*` 与逐元素 `.*` 现已保留不同源操作身份，并分别进入目标专属 runtime call plan；当前只
承诺二维矩阵乘法、完全相同 shape 的逐元素运算和 scalar expansion。`end` 和逻辑索引尚未进入
索引 IR，一般数组隐式扩展、矩阵除法/幂、command form、cell/struct/string 和异常结构尚未进入
当前可保持边界。因此文档、版本说明和 CLI 都必须继续使用“已验证子集”的表述。

## 产品语义边界

Matlab frontend 必须按照 Matlab 语义建立规范事实，不能先生成 JavaScript 再让其他目标
读取 JavaScript。生产链路固定为：

```text
Matlab physical source
  → Matlab logical source / token stream
  → matlab::ast（保留 Matlab 表面语法和版本信息）
  → HIR（规范名称、调用、控制和语言无关操作）
  → MIR（类型化 CFG、值、shape、storage、alias、effect）
  → JavaScript semantic plan / JavaScript LIR
  → renderer（只序列化）
  → JavaScript + source map + dependency manifest + compilation report
```

需要跨语言复用的能力应扩展 HIR/MIR 的强类型 operation 或 side table；Matlab 专属规则应由
language profile 或专属 AST/HIR fact 明确选择。禁止用源拼写字符串、renderer 中的语言猜测、
JavaScript truthiness 或嵌套 Array 的偶然行为代替 Matlab 语义。

## P0：语义可信的 Matlab2JS 核心

P0 完成前，产品定位保持“实验性已验证子集”。以下顺序按语义风险与用户代码出现频率排列。

### Grammar 与表达式

- [ ] 以 R2024a/R2024b 官方 grammar 建立产生式清单、版本 gate、错误恢复和稳定诊断编号
- [x] 让表达式 token/AST/HIR 明确保留 `*`、`/`、`\`、`^` 与 `.*`、`./`、`.\`、`.^` 的不同身份
- [ ] 完成共轭转置 `'`、非共轭转置 `.'` 与字符向量引号的表达式级上下文语法
- [ ] 完成关系、逐元素逻辑、短路逻辑、colon、函数/command 调用和转置/字符向量的上下文语法
- [ ] 完成 `switch/case/otherwise` 的 cell case；标量 numeric/logical/character case 已交付
- [ ] 完成 `try/catch`、`return`、`global`、`persistent` 和 `arguments` block 的语法与失败关闭边界
- [ ] 对尚未支持的 `classdef`、并行和工具箱语法提供产生式级诊断，不降级为错误表达式

### 数组与数值语义

- [ ] 建立类型化 NDArray contract：rank/shape/stride/layout、numeric class、值语义、view/owner、copy-on-write 决策
- [x] 实现二维矩阵乘法、相同 shape 逐元素 `+`/`-`/`.*`/`./`/`.\`/`.^` 及 scalar expansion 的首批规则
- [ ] 实现矩阵除法/幂、共轭/非共轭转置及完整 numeric class 规则
- [ ] 实现逐维相容或一侧 extent 为 1 的隐式扩展，并在 MIR 中显式保存 broadcast plan
- [ ] 实现 `end` 的逐索引位置语义、线性/位置/逻辑索引、重复索引、空索引与运行时 bounds
- [ ] 实现 indexed deletion、自动 growth、shape 变化、空数组和 assignment conformability
- [ ] 实现整数/浮点 numeric class、NaN/Inf/signed-zero、complex 和 sparse 的可验证表示
- [ ] 对 JavaScript `Number` 无法精确保持的整数、浮点和 complex 行为在 capability 阶段失败关闭或选择 runtime

### 函数、工作区与副作用

- [ ] 区分 script caller/base workspace、普通 function 独立 workspace 和 nested function 共享 workspace
- [ ] 实现 anonymous/nested function、function handle、捕获、逃逸和生命周期
- [ ] 实现 `nargin`/`nargout`、`varargin`/`varargout`、缺省输出、逗号分隔列表和多输出忽略位
- [ ] 建立 `global`/`persistent` 的 module/runtime storage 与初始化顺序
- [ ] 将动态调用、`eval`、`assignin`、`load` 等影响名称/工作区的行为标记为显式 effect 和 capability

### JavaScript runtime

- [ ] 将 Matlab runtime 从每文件内嵌 helper 收敛为版本化、可 tree-shake 的 ESM 包，同时保留无外部依赖模式
- [ ] runtime API 只消费 JavaScript LIR 已规划的类型、布局、ownership 与异常策略，不重新分析源语义
- [ ] 为 dense/sparse/complex/string/cell/struct/function handle 建立稳定内部 ABI 和互操作边界
- [ ] 记录全部 NPM/runtime 组件的版本、来源、完整性、许可证、SBOM 和安全更新策略
- [ ] 为运行时错误建立稳定 Matlab 类别、源位置回映和可诊断的 JavaScript Error 层级

## P1：常用工程代码覆盖

- [ ] char 与 string 的独立类型、Unicode/shape/拼接/比较和 conversion
- [ ] cell 的 `()`/`{}`、comma-separated list、嵌套赋值和展开
- [ ] struct/动态字段、struct array、字段读取写入和 shape
- [ ] function/script/package/class 文件解析，路径解析、私有目录和名称优先级
- [ ] 常用 array/math/statistics/string/IO intrinsic 的分层 catalog 与版本/参数 contract
- [ ] MAT 文件、文本/表格输入输出的可选 runtime adapter
- [ ] table、timetable、datetime、duration、categorical 的首批高频纵切面
- [ ] 对不支持的 toolbox API 提供确定性 capability report，不生成表面可运行但语义错误的代码

## P2：大型工程与对象系统

- [ ] `classdef`、value/handle class、继承、property/method/event、访问控制和生命周期
- [ ] package、namespace、import、class folder、startup/path 和多文件增量 compilation graph
- [ ] MEX/Java/.NET/native library boundary 的明确支持或拒绝策略
- [ ] 并行/GPU/codegen/toolbox 专属语义的插件式 capability 与 runtime provider
- [ ] 项目级缓存、并行 lowering、稳定 artifact schema 和可复现构建

## 每个能力的完成定义

任一复选框只有同时满足以下条件才可标记完成：

1. 记录官方 Matlab 版本、语法和运行语义依据，并给出支持/不支持边界。
2. 正向、边界、负向和版本 gate 用例先落地；无损经过 Matlab AST、HIR、MIR 和 JavaScript LIR。
3. Analyzer 的 type/shape/storage/alias/effect side table 完整，verifier 能拒绝缺失、陈旧和矛盾事实。
4. JavaScript capability/legalization 明确选择 direct、rewrite、runtime 或 unavailable；renderer 不做新决策。
5. 生成代码通过 Node.js 语法和执行测试；source map 能回到原始 `.m` 位置；诊断不输出半成品代码。
6. 授权环境中与目标 Matlab 版本差分；本地 oracle 只能补充，不能替代 reference runner。
7. 覆盖成功/拒绝 fuzz seed、确定性 mutation、资源上限和最小化回归样本。
8. 在真实规模基准上记录延迟、峰值内存、产物/runtime 体积和执行性能，不突破版本化预算。
9. 更新支持矩阵、专项计划、公开示例和用户可见限制；必要时记录 runtime 依赖和许可证。

## 测试与发布门禁

- [ ] 建立可授权的 Matlab runner：固定 release、license/runner 隔离、超时、日志脱敏和结果归一化
- [ ] differential corpus 按 scalar、array、function、workspace、object、error、library 分层，禁止只累计 happy path
- [ ] 每个 P0 operator/index feature 至少覆盖标量、空值、shape 边界、错误类型、求值次数和副作用顺序
- [ ] 增加语法/AST 结构 fuzz、表达式/shape 语义 fuzz、runtime differential fuzz 和历史崩溃 corpus
- [ ] source map 门禁覆盖续行、多语句行、local/nested function、生成 runtime wrapper 和异常栈
- [ ] 性能门禁覆盖大 dense array、broadcast、matrix multiply、section、函数图、冷启动和运行时包体积
- [ ] 发布报告自动生成 Matlab feature manifest、reference 版本、差分 case 数、已知限制和性能变化
- [ ] P0 全部完成且连续发布门禁稳定后，才评估从“实验性子集”提升产品成熟度标记

## 当前迭代记录

- [x] 完成专项审计并建立本计划，明确当前不是完整 Matlab 2024 实现
- [x] 交付标量 numeric/logical/character `switch/case/otherwise`，selector 只求值一次且无 fallthrough
- [x] 修正选择语义的源语言耦合：Matlab 字符 case 使用精确相等，Fortran 继续使用补空格比较
- [x] 增加 lexer、双后端、拒绝、差分示例和 Matlab fuzz seed
- [x] 运算符身份与矩阵/逐元素基础 lowering：双目标 LIR helper plan、runtime、正负测试、差分示例和 fuzz seed
- [ ] 随后纵切面：`end`、逻辑索引和隐式扩展

## 官方语义索引

- [Array vs. Matrix Operations](https://www.mathworks.com/help/matlab/matlab_prog/array-vs-matrix-operations.html)
- [Compatible Array Sizes for Basic Operations](https://www.mathworks.com/help/matlab/matlab_prog/compatible-array-sizes-for-basic-operations.html)
- [Array Indexing](https://www.mathworks.com/help/matlab/math/array-indexing.html)
- [`end`](https://www.mathworks.com/help/matlab/ref/end.html)
- [`switch`](https://www.mathworks.com/help/matlab/ref/switch.html)
- [Command vs. Function Syntax](https://www.mathworks.com/help/matlab/matlab_prog/command-vs-function-syntax.html)
- [Base and Function Workspaces](https://www.mathworks.com/help/matlab/matlab_prog/base-and-function-workspaces.html)
- [Local Functions](https://www.mathworks.com/help/matlab/matlab_prog/local-functions.html)
- [Data Types](https://www.mathworks.com/help/matlab/data-types.html)
