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
| 表达式 | 标量与数组算术/关系比较、静态满秩稠密实数方阵/超定/欠定求解、safe-integer 方阵幂、调用、向量/矩阵字面量、转置、静态及运行时 extent 的上下文 `end`、colon/index/section、保序/重复/空 numeric selector、线性及逐维 logical selector | 秩亏求解的 Matlab 兼容行为、非整数矩阵幂、complex 转置、短路/逐元素逻辑完整语义 |
| 数据模型 | number、boolean、字符文本的当前子集、矩形嵌套数组 | numeric class、complex、sparse、string、cell、struct、table、datetime、对象 |
| 数组语义 | 1-based、列主序、静态 N 维 reshape/section，以及静态 shape 或 local-function runtime rank/extent 的 compatible-size 隐式扩展；二维矩阵乘法、静态满秩实数 square/rectangular solve、方阵 integer power、广义 selector 读写 | 一般 NDArray/typed-array 表示与动态 shape 数据流、秩亏/一般矩阵分解与幂、删除、growth、完整动态 bounds、值语义和 alias 契约 |
| 函数 | 文件级 local function、前向调用、单/多输出 | `nargin`/`nargout`、`varargin`/`varargout`、function handle、anonymous/nested closure、workspace |
| JavaScript runtime | 内嵌数组、广义 selector/section、reshape、flatten-once 广播、转置、矩阵求解/幂和基础 intrinsic runtime | 有版本的 Matlab runtime 包、NDArray 表示、数值/异常兼容层、依赖与许可证审计 |
| 验证 | Node.js、生成 C++、oracle、source map、专项 fuzz seed、Matlab 编译性能发布阈值 | 授权 Matlab reference runner；真实项目 corpus；运行时性能、数值精度和内存发布阈值 |

矩阵 `*` 与逐元素 `.*` 保留不同源操作身份，并分别进入目标专属 runtime call plan。0.4.8
进一步交付静态已知 shape 的 Matlab-first-dimension 隐式扩展、数组关系比较、vector/rank-2
转置、逐索引位置 `end` 以及单个线性 mask 的逻辑读写；这些事实均无损通过 HIR、MIR 和目标
LIR。0.4.9 又为静态满秩稠密实数矩阵交付方阵部分主元求解、超定最小二乘、欠定最小
范数、矩形右除以及 safe-integer 正/零/负方阵整数幂，并以强类型 matrix-operation/solve
plan 贯穿各层；索引进一步覆盖保序和重复 numeric selector、empty selector、线性/逐维
logical selector，并在生成 runtime 中验证动态已知的 mask extent。0.5.0 将 `end` 的静态常量快路与
动态 axis/linear extent plan 贯穿 HIR、MIR 和两套目标 LIR；compatible-size 运算另以
static-extents/runtime-operands shape source 区分编译期快路与 local function 参数的运行时 rank/extent，
两套 runtime 独立验证 rectangularity、singleton 兼容性及标量/数组结果。一般 NDArray 表示与跨函数
动态 shape 数据流、complex 共轭、growth、秩亏系统的 Matlab 兼容求解、非整数矩阵幂、
command form、cell/struct/string 和异常结构仍不在当前可保持边界。因此文档、版本说明和 CLI
必须继续使用“已验证子集”的表述。

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
- [x] 完成共轭转置 `'`、非共轭转置 `.'` 与字符向量引号的表达式级上下文语法
- [ ] 完成关系、逐元素逻辑、短路逻辑、colon、函数/command 调用和转置/字符向量的上下文语法
- [ ] 完成 `switch/case/otherwise` 的 cell case；标量 numeric/logical/character case 已交付
- [ ] 完成 `try/catch`、`return`、`global`、`persistent` 和 `arguments` block 的语法与失败关闭边界
- [ ] 对尚未支持的 `classdef`、并行和工具箱语法提供产生式级诊断，不降级为错误表达式

### 数组与数值语义

- [ ] 建立类型化 NDArray contract：rank/shape/stride/layout、numeric class、值语义、view/owner、copy-on-write 决策
- [x] 实现二维矩阵乘法、相同 shape 逐元素 `+`/`-`/`.*`/`./`/`.\`/`.^` 及 scalar expansion 的首批规则
- [x] 实现当前非 complex vector/rank-2 的共轭/非共轭转置；complex 共轭和完整 numeric class 仍待建设
- [x] 实现静态满秩稠密实数方阵/超定/欠定矩阵的左除/右除，以及 safe-integer 正/零/负方阵整数幂；双目标使用同一 solve-kind 与失败关闭边界
- [x] 以部分主元处理方阵，以带列主元 Householder QR 处理超定最小二乘和欠定最小范数；秩亏系统在 runtime 确定性失败关闭
- [ ] 实现秩亏系统的 Matlab 兼容求解/警告策略、非整数矩阵幂及完整 numeric class 规则
- [x] 实现静态 shape 及 local-function runtime rank/extent 下逐维相容或一侧 extent 为 1 的隐式扩展，并在 HIR/MIR/目标 LIR 显式保存 shape source 与 broadcast plan
- [x] 实现静态 extent 的 `end` 逐索引位置/线性 numel 语义
- [x] 实现保序/重复 numeric selector、empty selector、线性/逐维 logical selector 和标量/vector 写入；静态可证 shape 在 Analyzer 拒绝，不可静态确定的 logical extent 在生成 runtime 验证
- [x] 实现动态 `end` 的 axis/linear extent 解析；完整动态 bounds、indexed deletion、自动 growth 和动态 shape 变化仍待建设
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
- [x] 0.4.8 性能门禁增加 N 维 broadcast、转置、数组比较、逻辑索引和 `end` 的专属编译延迟、吞吐与产物大小预算；0.4.9 增加矩阵 solve/power、逐维 logical 与重复 numeric selector 编译场景；0.5.0 增加动态 `end` 读写和 runtime-shape broadcast 场景
- [ ] 性能门禁继续覆盖大 dense array 执行、matrix multiply/section runtime、冷启动和运行时包体积
- [ ] 发布报告自动生成 Matlab feature manifest、reference 版本、差分 case 数、已知限制和性能变化
- [ ] P0 全部完成且连续发布门禁稳定后，才评估从“实验性子集”提升产品成熟度标记

## 当前迭代记录

- [x] 完成专项审计并建立本计划，明确当前不是完整 Matlab 2024 实现
- [x] 交付标量 numeric/logical/character `switch/case/otherwise`，selector 只求值一次且无 fallthrough
- [x] 修正选择语义的源语言耦合：Matlab 字符 case 使用精确相等，Fortran 继续使用补空格比较
- [x] 增加 lexer、双后端、拒绝、差分示例和 Matlab fuzz seed
- [x] 运算符身份与矩阵/逐元素基础 lowering：双目标 LIR helper plan、runtime、正负测试、差分示例和 fuzz seed
- [x] 0.4.8 纵切面：静态 N 维隐式扩展、数组比较、vector/rank-2 转置、上下文 `end`、线性逻辑索引与双目标 runtime
- [x] 为上述能力加入跨层 verifier、五组双目标差分示例、负向语义测试、fuzz seed 和专项性能发布预算
- [x] 0.4.9 纵切面：静态满秩稠密实数方阵/超定/欠定 `\`/`/`、safe-integer 方阵 `^`、部分主元/CPQR 双目标 runtime、MatrixOperationPlan/solve kind 与差分/fuzz/性能门禁
- [x] 0.4.9 索引纵切面：逐下标 selector plan、保序/重复/空 numeric selector、线性/逐维 logical selector、动态 mask runtime 验证与双目标差分/fuzz/性能门禁
- [x] 0.5.0 索引纵切面：静态 `end` 常量快路、动态 axis/linear extent side table、HIR/MIR/LIR verifier、双目标独立 runtime、差分/source map/fuzz/性能门禁
- [x] 0.5.0 广播纵切面：static-extents/runtime-operands shape source、未知 rank 显式计划、双目标矩形 shape 推导、标量/数组泛型调用、关系比较、拒错及差分/source map/fuzz/性能门禁
- [ ] 随后纵切面：秩亏求解兼容策略、complex/numeric class、growth 与 indexed deletion

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
