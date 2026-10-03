# Matlab 输出存在性与返回边界

本文固定当前实现和 0.8.0 发布前剩余合同。调用 count、typed `~` receiver 已交付；
**条件未赋值输出、presence-aware return/receiver 和完整 `ans` 仍未交付**。
不能把只读分析完成当作这些运行语义完成，也不能提前放宽函数输出确定赋值检查。

## 当前：独立输出赋值状态分析

`src/ir/output_assignment.*` 实现 revision-bound `OutputAssignmentTable` v1。
它在最终优化 MIR 和已验证 alias/effect 之后运行，由 `AnalysisManager` 缓存；生产 Matlab
编译报告提供 `mir-output-assignment` stage。分析不改变 MIR、函数签名、目标 ABI 或生成代码。

- 每个命名输出保存 ordinal、`SymbolId`、workspace `StorageId`/type/shape、caller result
  type/shape，区分正文工作值与出口转换后的值；没有 workspace 写入的输出不凭空获得存储或初值。
- `unreachable`、`unassigned`、`assigned`、`path_dependent` 表示一组可能的绑定状态。
  “已赋值”路径与“未赋值”路径合流后必须是 `path_dependent`，不是 Boolean `true`。
- 只有实际成功的绑定写入才将该路径变为 `assigned`。直接 store、indexed store、writeback、
  loop/catch binding 使用 MIR 指令和精确 memory-access 来源；unknown-memory 或 callee 的
  may-write effect、输出校验 temporary、`~` receiver 都不证明输出绑定已经初始化。
- 共享 raw input/output 在 entry argument storage 相同时已赋值；输入正规化产生的独立 formal
  workspace 则以实际初始化 store 为界。不能把 raw 参数存在性与 formal materialization 混为一谈。
- normal successor、backedge、零次循环和不可达 block 参与固定点。每个 may-fail 指令将其
  **执行前**状态传入当前 exception handler；失败写入没有提交，不能把 block 的最终状态传给 catch。
  `store_indexed` 的最低 effect 包含 `may_fail`，尺寸/索引/替换失败因此不会被隐藏。

函数、block、instruction 行使用稠密强类型身份；状态放在连续的 byte-sized cell buffer，
写入来源使用单独的平坦清单，避免每条指令单独分配状态 vector。生产算法用依赖 worklist
传播四态格；独立 verifier 对每个输出探索 `(block, assigned Boolean)` 的至多两种状态，
复核真实路径结果、异常边、库存、零号哨兵、row offset、binding/type/storage 与 write provenance。
两种算法不会共享计算出来的状态。`dump_output_assignments` 提供确定性 v1 调试输出。

这只是可供 lowering 使用的证明与来源信息，**不是运行时 presence flag 或可执行 Boolean SSA**。
普通正文未赋值读取的诊断保持不变；当前前端仍拒绝不确定赋值的命名返回值。

条件输出的另一项前置基础是 mutable CFG value join。它现以实际 incoming SSA value
重建 phi type/shape，而不沿用绑定首次声明的 storage metadata；同类型数组 extent
变化已可在两个目标执行，scalar/array 混合重新绑定可在 JS 执行，C++ 在自己的能力
边界拒绝不能表示的 type/rank。生产 worklist 与独立 SCC proof 分开计算。这个
修复仍不生成 presence Boolean，也不把 unknown/top 类型解释为 absent；完整动态
workspace/NDArray 表示、typed use 精化与 C++ 动态值 ABI 仍待后续交付。

## 待实现：可执行 presence 与两阶段出口

下一阶段必须从上述真实绑定/写入来源建立独立可执行合同，而不是在 Emitter 中猜变量名：

1. Entry 初始化输出 presence；共享已初始化 input/output 为 present，其余为 absent。
   每次成功写入才更新，normal join 使用路径选择的 typed Boolean merge；exception handler
   必须观察实际抛错点的状态。不能用 `0`、空数组或默认构造的 `T` 冒充缺失输出。
2. 函数所有正常/提前返回进入正文 handler 外的统一出口。第一阶段按声明顺序，仅对
   present 输出执行 class/size normalization 和 validators；全部已赋值输出都参与，哪怕未请求或被 `~` 忽略。
3. 第一阶段全部成功后，第二阶段按完整 requested prefix 检查 missing output，包括 `~` 的位置。
   缺失错误使用原生 `MATLAB:unassignedOutputs`。不能逐槽交错执行 validator 与 missing check：
   原生“缺失首输出、已赋值但非法的第二输出”先产生第二输出 validator 错误。
4. 独立 JavaScript/cpp LIR 固化 presence-aware return bundle、投影/receiver、一次调用、异常
   与源码映射。没有静态可表示类型的槽不能让公共 MIR 拒绝合法 JavaScript；C++ 目标能力和
   动态值表示必须在自己的 lowering 边界处理，不建立两目标之间的生成依赖。
5. 括号与裸零需求调用仅在调用成功且首值存在时更新 `ans`；void、absent、validation failure
   保持旧 `ans`。隐式接收首值与传入 callee 的 count `0` 是不同合同。

各层 verifier 必须独立拒绝 presence/slot/source/CFG/类型/异常位置污染；重新规划私有 LIR
不能掩盖来源镜像损坏。Analyzer 的边界放宽只能与 MIR guard、两个目标执行、严格 C++、
原生 R2024b 对照、fuzz 和未放宽的性能预算一起交付。

## 原生证据与发布边界

[R2024b Update 10 run 37086893737](https://github.com/abandoft/mpf/actions/runs/37086893737)
在提交 `8f3ca74238f8d59e7bcfcd804bfe1a37cedffe91` 上实际执行了扩展后的
`invocation_context.m`，并核验 JavaScript/严格 C++ 的 55 个输出 token、原始 snapshot bytes、
完整 SHA/provenance 和 27 项 native observations；所有实际步骤成功。同一提交的七类 CI 也全部成功。
原始源码与快照的 SHA-256 均为
`a3145e122d0693f84cd18ebb70e61a8057ebfc6b2431c975d543ced3242d794e`。

该 run 证明已经实现的 receiver/count/default/assigned-output validation 行为。
27 项 observations 仍包含 MPF 尚未实现的条件缺失值与完整 `ans`，不能将采集结果或
synthetic verifier fixture 冒充这些功能的 parity。28 个已建模标准 validator 的失败 identity、
输入 position/输出名称和已核验类型前置错误已接入两个独立 runtime；129 项原生观察冻结于
[run 37091387836](https://github.com/abandoft/mpf/actions/runs/37091387836)，后续独立目标
fixture 复核 80 项失败与两组文本接受，空 char 保留 R2024b 的 numeric class 拒错。
真实 run 37093296126 因两个目标/旧示例错误接受空 char 而失败，不能记成原生 parity
已通过；已按该证据修正 runtime 和空 numeric 示例。
[run 37094163608](https://github.com/abandoft/mpf/actions/runs/37094163608) 在完整 SHA
`8dce8ea5b21e63cc2c9a534858c13bad80a05062` 实际核验修正后的 82-token fixture，
所有原生/双目标执行与验证步骤成功，另采集并冻结 168 项空 char/空 double 观察。
这些观察进一步固定 `0×0` 空 char 的 shape validator 行为；两个目标已修正，
当前 194-token fixture 增加对应 112 项输入/输出执行，由
[run 37095935051](https://github.com/abandoft/mpf/actions/runs/37095935051) 在完整 SHA
`4b43f1112e5d42c2221fc3006297bfe43346ca54` 实际验证通过。已回读 artifact，三个运行目标
输出一致，原始/snapshot SHA-256 同为
`deae270dd63614cb0f192e59be873c905f8fb61b8a0199f5b2bac7f5075fb7cf`。
空值校验修复不是条件输出 presence 的交付证明。
完整 class/size/custom validator 异常和所有类型
组合仍待对齐；消息尾部不宣称原生本地化文本的逐字等价。
0.8.0 的发布仍等待这些剩余验收及 release workflow 接入，当前不创建发布标签。
