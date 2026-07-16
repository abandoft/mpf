## 0.4.1

- 新增 profile 驱动的 `ScopeModel` 与 revision-bound `NameScopeEdges`；function、statement、body、alternative scope 均绑定 HIR owner/kind 并由 verifier 检查稠密性、父子关系和错误 scope 注入，不再把所有语言强制压成函数级作用域。
- TypeScript `let`/`const` 支持真实嵌套 lexical block、同名遮蔽和向最近外层可写 binding 赋值；Analyzer 在分支/循环 scope 中隔离局部符号并合并外层确定赋值状态，离开 block/`for` 后访问局部名称、`for (let i = i; ...)` 的 TDZ 自引用和当前不可移植的 nested function 都稳定失败关闭。
- 双目标 identifier inventory 从 spelling-only 扩展为 `SymbolId` identity + source spelling 合约；同一词法名可在不同 scope 保持可读复用，同一 symbol 的冲突 spelling、保留字碰撞和缺失 identity 在 renderer 前被确定性拒绝。
- JavaScript/`cpp` 私有 LIR schema 升至 v12，为 expression、statement、parameter、return、multi-target 和 declaration 保存强类型 symbol identity，并为 statement/body/alternative 保存目标专属 `ScopePlan`；架构门禁禁止 renderer 恢复按源码拼写猜测绑定。
- 两个 renderer 现在按 LIR lexical scope plan 在实际 brace block 内序列化声明；TypeScript 可在内外 block 对同名名称使用不同类型，生成 JavaScript 与严格 C++17 都保持正确生命周期、遮蔽和外层更新语义。
- TypeScript parser 新增规范 C-style `for (let ...; condition; update)` 纵切面，覆盖 type annotation、`++`/`--`、`+=`/`-=`/直接赋值、`break`、`continue` 和 induction scope；`const` induction、非布尔条件或更新其他 binding 会产生稳定诊断。
- HIR→MIR 为 TypeScript `for` 建立独立 preheader、condition、body、update、exit block，initializer/update 使用显式 `store`，回边与 break/continue edge 携带 storage version actual；`continue` 明确进入 update block，而不是依赖 renderer 重写控制流。
- TypeScript 数字统一按 ECMAScript `number` 的实数逻辑类型分析，数组索引仅接受可证明为整数且在目标整数范围内的常量，避免 C++17 隐式截断小数；数字参数、循环和 typed array 继续通过 MIR 类型相容验证。
- JavaScript runtime dependency discovery 改为按 semantic profile、comparison form、truthiness 与容器需求精确裁剪；TypeScript 原生严格标量比较不再无条件携带 Python dynamic equality helper。
- 新增 TypeScript block-scope 与 canonical-for 可执行样例、fuzz seeds，以及 Node.js source、生成 JavaScript、生成 C++17、声明式 oracle 四路差分；block-local 混合类型遮蔽、外层写入、break/continue、循环退出和 runtime tree shaking 都有端到端断言。
- 单元门禁新增 NameScope graph/损坏输入、SymbolId inventory/collision、MIR `for` CFG/store/continue edge 和双目标 scope plan 验证；golden 与架构依赖检查同步固定 LIR v12 合约。
- 项目版本、性能基线、安装示例和全部现行文档同步到 0.4.1；`CHANGELOG.md` 直接以正式版本标题开头，不设置 `Unreleased` 占位段，release verifier 现在会准确统计含 ASCII 分号的 8—20 条更新。
- 当前批次为 172 项内部测试、53 个差分 case 和 64 项 CTest；工具完整环境覆盖 149 条程序执行路径，Debug/Release、ASan/UBSan、format/clang-tidy、四语言 fuzz smoke、双后端隔离、安装消费和六场景性能门禁均已通过；生产代码行覆盖率实测 89.39%（20566/23008），高于 85% 硬门槛。

## 0.4.0

- 新增公共 `SourceLanguage::typescript`、`typescript`/`ts` 名称、`.ts`/`.mts`/`.cts` 扩展探测和 1.0—6.0 manifest；frontend registry、CLI 帮助、自动检测、availability/name API 与安装后 catalog 统一从 descriptor 获取第四种源语言，不把 TypeScript 塞入 Python 或 JavaScript 模式。
- 新增独立 TypeScript full-source statement lexer，直接处理 brace、semicolon/newline、单/双引号 escape、`//`/`/* */` comment、strict equality 与绝对 byte span/source location；未闭合 comment/string 和控制字符使用新的 `MPF19xx` 诊断空间，template literal 等未建模 token 失败关闭。
- 新增编译期独立的 `typescript::ast::LanguageTag`、PMR arena `Program`/`Expression`/`Statement`、稠密 `AstNodeId` inventory、确定性 dump、AST verifier 和 AST→HIR + revision-bound semantic seed lowering；四种 frontend artifact 互不兼容，TypeScript 同样经过统一 HIR/MIR/双目标 LIR 五层生产路径。
- TypeScript parser 首个纵切面覆盖单 declarator typed/untyped `let`/`const`、标量与 homogeneous typed array、普通/indexed assignment、`console.log`、function/annotation erasure/default/value return、braced `if`/`else if`/`else`、`while`、`break`/`continue` 和表达式调用；parser 在构造时直接驻留本语言 arena，不建立共享递归 syntax tree。
- TypeScript semantic profile 固化 real quotient、parameter default 与 explicit-only export policy；Analyzer 的 real division、default parameter 分析/调用补齐、参数 annotation seed 和 operand logical 判断改为 profile 驱动，HIR→MIR 对所有 value-returning frontend 使用统一函数结果 contract，不再把这些能力写死为 Python 特例。
- MIR verifier 明确接受 integer/real 数值实参与实数逻辑签名的合法相容关系，同时继续拒绝非数值类型、shape、reference 和 function signature 分歧；typed `number` 参数、默认值和调用因此能在严格 verifier 下直接生成 JavaScript 与可编译 C++17，而不是绕过类型检查。
- 显式函数 export 从 TypeScript AST statement fact 进入 HIR `SemanticTable`、MIR `Function` 和 JavaScript LIR ABI/resource verifier；双目标 artifact schema 升至 LIR v11，ESM 只导出 `export function`，普通 TypeScript 顶层函数保持 module-private，其他既有 frontend 继续使用原有 top-level export policy，纯 emitter 不读取源语言身份。
- 当前可移植 TypeScript 子集对 loose `==`/`!=`、`var` hoisting、arrow/template、optional-without-default、block-local declaration、未声明赋值、const rebinding、非布尔逻辑/控制条件和对象/array reference comparison 给出稳定拒绝诊断，避免 JavaScript 与 C++17 静默产生不同运行语义。
- typed array 纵切面保持零基读取/写入、const container 元素 mutation、element/shape 分析和 JavaScript Array/C++ `std::vector<double>` 两种私有目标表示；TypeScript 不自动继承 Python/Matlab/Fortran 的全局 intrinsic spelling，标准库与宿主 API 留待显式 source binding。
- 新增 TypeScript descriptor/arena/conformance、lexer/source-location、版本/扩展检测、strict comparison、default/control/export、拒绝语义与 typed-array 单元/集成门禁，以及 `basic.ts`/`arrays.ts` 的 Node.js 24 source、生成 JavaScript、生成 C++17 和声明式 oracle 四路差分；编译器分层门禁同步要求第四个 frontend 直接构造语言 arena。当前为 167 项内部测试、51 个差分 case 和 62 项 CTest，Debug/Release、ASan/UBSan、format/clang-tidy、六场景性能门禁全部通过，生产代码行覆盖率实测 89.20%（19587/21959）。

## 0.3.9

- MIR 新增与 `Program::revision` 绑定的稠密 `OperationAttributeTable`，expression/statement arena 节点只保留结构边、resident instruction 和强类型 ID；spelling、强类型 comparison/binding/intrinsic、调用结果策略、索引规则及语句 operation 属性由独立行按 `MirExpressionId`/`MirStatementId` O(1) 查询。
- type、element type、shape、layout、tuple element 与赋值目标事实统一以驻留 `TypeId`/`ShapeId` 表达；`Function` 同时持有 parameter/result type 与 shape 签名，删除 flat MIR 对 HIR `ValueType`/extent/intent/return/assignment metadata 的宽字段镜像。
- HIR→MIR lowering 原子地把递归 sequence metadata 和 assignment pattern 转换为强类型 MIR 属性，把参数 intent/optional 和函数结果落到 storage/function contract；属性表 verifier 拒绝 stale revision、非稠密 inventory、错误 origin、非法强类型 ID、tuple arity、target arity 和 pattern 不变量。
- 公共 target LIR builder、code-binding 验证、JavaScript/`cpp` semantic lowering 和 `cpp` capability validator 全部迁移到窄 MIR + 属性表合约；两个后端只从 type/shape/storage/function/call-site 强事实重建自己的私有 LIR，不再读取已删除的源语义字段。
- conditional、Python `and/or` 和 comparison chain 在 MIR 自身建立显式 lazy CFG：条件先产生 `truthiness` operation，分支表达式只驻留在对应 successor，比较链逐项产生强类型 `compare` 并在失败边短路，最终通过 typed block argument/edge actual 合并结果和 storage SSA version。
- MIR opcode 删除含义宽泛的 assignment/indexed-assignment 身份，新增 `load`、`allocate`、`store` 与 `store_indexed`；变量读取、未初始化声明、单目标和多目标写入分别形成可验证的 runtime-independent memory operation，多目标 store 携带稳定 result ordinal 和独立 storage。
- writable section actual 在 user call 前后显式生成 `copy`/`writeback` 指令；copy-out 与 copy-in/out 具有不同 operand contract、expression-lifetime temporary storage 和强类型 transfer mode，call-site 仍保存原始 root/view/lifetime/writability 供目标 ABI 与 overlap 分析消费。
- alias/effect analysis 识别 load/store/copy/writeback 的真实 read/write/allocate 集合；MIR verifier 新增 lazy merge、truthiness/compare、allocate/store、copy/writeback、函数 shape 签名与动态 extent edge compatibility 检查，并通过一次构建的稠密 call/value/block-argument 索引保持线性验证成本；opcode contract 与 1100 余行 verifier 分别拆入专属组件，lowering 主文件不再混入验证实现。
- 确定性 MIR textual dump 升级为 v3，公开 operation attribute revision、binding/comparison/lazy 标记、函数 parameter/result shape、transfer/comparison/truthiness/result-index 及显式内存操作；架构门禁禁止宽语义字段或旧 assignment opcode 回流 flat 节点。
- 新增 lazy CFG、typed merge、load/store、copy-in/out、copy-out、temporary/writeback effect 与 operation-table stale/corruption 负向测试，内部测试增至 161 项；49 个差分 case、60 项 CTest、双后端隔离、fuzz、质量、Sanitizer 和性能门禁通过，生产代码行覆盖率实测 89.68%（18785/20946）。

## 0.3.8

- MIR 新增强类型 `MirExpressionId`/`MirStatementId`，把原递归 `Expression`/`Statement` 兼容树改为带零号哨兵的稠密 arena；expression child、statement body/alternative 与 program root 只保存 ID，不再按 HIR 形状递归拥有节点。
- 每个 MIR expression arena 节点显式绑定唯一 resident instruction、SSA `ValueId`、`TypeId`、`ShapeId` 与 `StorageId`；lowering 先驻留子值并按确定的左到右顺序形成 instruction operands，缺省 slice operand 以无效强类型 ID 保留位置而不伪造值。
- 每个 MIR statement arena 节点显式绑定 operation instruction；函数、选择、循环、恢复后的控制 body 与顶层顺序通过 `MirStatementId` ownership graph 表达，生产 `Program` 新增独立 roots inventory，删除后端对递归 MIR statement 容器的依赖。
- MIR verifier 新增 expression/instruction opcode、origin、result、type、shape、storage 与 operand 逐项一致性检查，并验证 operation/instruction identity、根可达性、唯一 ownership、cycle、非法子边和未驻留节点，损坏 arena 在 backend 前失败关闭。
- 双目标公共 LIR builder 改为从 MIR ID arena 进行 O(1) lookup，再分别构造 JavaScript LIR 与 `cpp` LIR；目标递归结构只存在于各自私有 LIR，公共 lowering 不再从 `source.statements` 复制 MIR 兼容树。
- JavaScript/`cpp` semantic lowering、runtime feature discovery 和 selector/default traversal 全部切换到 flat MIR view；`cpp` capability validator 直接使用 MIR function/call-site graph 判断递归，并在 ID arena 上执行 return、类型、slice 与 comparison 验证。
- code-binding 验证直接扫描唯一 MIR expression inventory，删除重复的递归 statement walker；确定性 MIR textual dump 升级为 v2，公开 root、expression/operation ID、resident instruction、child edge 与 SSA/type/shape/storage 对应关系。
- 架构门禁拒绝恢复 `vector<Expression> children`、`vector<Statement> body/alternative` 或 target builder 的 `source.statements` 路径；新增 flat arena 稠密性、instruction correspondence、非法 expression edge、重复 operation root 与 dump inventory 测试，内部测试增至 160 项；生产代码行覆盖率实测 89.65%（17987/20063）。

## 0.3.7

- Python statement parser 不再构造共享递归 syntax tree，直接以语言专属 `python::ast::Statement` draft 和 `AstNodeId` 边生成 PMR arena AST；参数默认值、assignment target、控制流 body 与恢复后的根节点都只物化一次。
- Matlab statement parser 同步切换为直接 arena 构建，函数、多输出、`if/elseif/else`、循环、indexed assignment 与恢复路径在 parser 阶段即形成 `matlab::ast` 节点，不再经过跨语言 `Statement` 容器。
- Fortran free/fixed-form 后续 parser 直接生成 `fortran::ast`，procedure、声明、`SELECT CASE` selector、DO/IF 和 writable target 均以语言节点及强类型 ID 连接，保留版本门控与错误恢复。
- 新增共享机制而非共享语法的 `FrontendAstBuilder<LanguageTag>`：递归表达式解析后立即驻留、统一分配稠密 ID/record、预留 statement/expression/root inventory，并把所有顶层 arena 容器绑定到 parser session memory resource。
- 三个 parser API 返回编译期互不兼容的 `python::ast::ParseResult`、`matlab::ast::ParseResult` 与 `fortran::ast::ParseResult`；frontend session 直接发布对应 variant artifact，删除 parse→共享 program→copy-to-arena 的第二次整树遍历。
- 删除旧 `compiler::Program`/`Statement`/`ParseResult`、`make_*_ast` 转换器和未被生产驱动使用的 parser facade；公共跨层 statement 身份收敛到独立轻量 `statement_kind.hpp`，避免宽 syntax 结构重新成为扩展接口。
- 删除只服务旧 syntax tree 的递归 function-graph 实现与 code-binding overload，测试分别通过真实 HIR generic graph 和 MIR binding contract 验证；`mpf-core` CMake 编译面同步移除废弃源文件。
- 架构门禁现在要求三个 parser 直接使用 arena builder、以 `AstNodeId` 返回 block，并拒绝恢复旧 scratch/facade 文件；新增三语言错误恢复、AST 可达性、session allocator ownership 和强类型 ParseResult 测试，内部测试增至 159 项；生产代码行覆盖率实测 89.74%（17788/19822）。

## 0.3.6

- Python 表达式 lexer/parser 新增 `is`/`is not` 与 `in`/`not in` 的专用 token、复合操作符识别和真实 comparison precedence；前置 `not` 正确包裹完整比较表达式，链式比较继续保持逐项短路。
- 比较操作符从字符串升级为贯穿语言 AST、窄 HIR、MIR 和双目标 LIR 的强类型 `ComparisonOperator`；frontend AST schema 升至 v3，各层 verifier 拒绝二义 binary payload、空操作符和错误 chain arity。
- Analyzer 明确 Python equality 边界：布尔与数值按 Python 数值规则比较，同类 list/tuple 递归逐元素比较，list 与 tuple 即使内容相同也不相等，异类标量相等安全返回 false。
- identity 支持 `None`/布尔 singleton 和 JavaScript sequence 引用身份；数值/string 对象驻留规则以 `MPF2045` 拒绝，`cpp` 对 value-container 无法保持的 sequence identity 以 `MPF2044` 失败关闭。
- membership 支持 substring、list 与异质 tuple，使用 Python equality 逐元素判断，并覆盖 `not in` 和 membership/identity 混合比较链；不受支持的 container 或 string needle 类型在 Analyzer 阶段稳定拒绝。
- JavaScript runtime 使用私有 `Symbol` 标记 tuple，在保留 Array 性能和引用身份的同时区分 list/tuple equality；`__mpf_py_equal`、`__mpf_py_is` 与 `__mpf_py_contains` 由目标 representation 显式选择。
- `cpp` runtime 新增递归跨元素类型的 list/tuple equality、string/list/tuple membership 和 singleton identity；二元比较由 LIR 临时资源与 reference-lambda IIFE 固定左到右单次求值，不依赖 C++17 未指定的函数实参求值顺序。
- 双目标 LIR artifact schema 升至 v10，新增强类型 equality/ordering/identity/membership representation 与 verifier negative；新增 Python comparison 示例、fuzz seed 及 CPython/Node.js/严格生成 C++/oracle 差分，当前为 158 项内部测试、49 个 corpus case、60 项 CTest，生产代码行覆盖率 89.69%（17959/20023）。

## 0.3.5

- MIR 类型系统新增驻留的 tuple、function 与 reference 类型，函数保存独立签名，Python tuple 返回保持单结果、Matlab 多输出保持多结果，Fortran `INTENT(IN/OUT/INOUT)` 参数进入带模式的 reference type。
- 新增显式 call-site 表，关联 caller/callee、call instruction、argument/result type、optional omission、requested result 和 actual storage；verifier 跨函数检查 call/return、多结果、必需参数及 OUT/INOUT writable storage，确定性 MIR dump 同步输出类型签名和调用边。
- AST→HIR lowering 改为原子产出窄结构 HIR 与 revision-checked 稠密 `SemanticTable` seed；删除 HIR 中重复的 type/shape/binding/call/assignment facts、共享 syntax→HIR 兼容旁路和 HIR-only reindex。Analyzer 在任何 pass 前验证 seed 并原位完善它；参数关联引起的默认值克隆、optional omission 和重排会提升 revision，并将 HIR ID 与 facts 同步紧凑重映射。frontend conformance 同时验证并比较 HIR 与 semantic seed，架构门禁阻止宽字段和旁路恢复。
- lexical scope tree、声明/参数/结果/循环变量、遮蔽、引用和 builtin 解析迁入独立、只读 HIR 的稠密 `NameTable`；新增强类型 `ScopeId`，Analyzer 删除字符串符号哈希状态并改为按 `SymbolId` 访问，同时以 `FlowTable` termination facts 驱动确定赋值合流。
- alias/effect 从 MIR storage/instruction 和 lowering builder 中移出，形成 revision-bound、可由 `AnalysisManager` 缓存的 `AliasEffectTable`；稠密保存 storage root/escape、instruction local/transitive effect 与 read/write set、函数参数读写/escape fixed point，稀疏保存 alias relation 和 call-site 实参实例化。NameTable 派生的 `SymbolId` 让跨函数全局 storage 共享身份，调用摘要不会把纯局部写入误报为调用者可见写入；未知外部调用保守读写 unknown storage 并 may-fail。独立 verifier/dump 拒绝 stale、inventory 不符、弱化事实和非 fixed-point 摘要。
- backend descriptor 升级到 API v5，capability、lowering 与 conformance harness 必须同时接收 MIR 和已验证 alias/effect facts；JavaScript/`cpp` semantic plan 汇总函数 effect/unknown-memory 信息，两个后端均拒绝陈旧分析输入。
- MIR call-site 删除 `argument_types`/`argument_storages`/`argument_omitted` 三组并行数组，改为单一参数区域对象；显式区分 value、read-only borrow、OUT/INOUT mutable borrow、copy-out、copy-in/out、optional forwarding 与 omitted，并记录 root/view/lifetime/writability。alias/effect table 新增参数读写/逃逸和成对 overlap facts，拒绝多个可写实参的保守重叠；optional parameter storage 与 global/borrowed/expression/module lifetime 进入 verifier。
- JavaScript 与 `cpp` 私有 LIR 保存 MIR 已决定的 argument transfer plan；JavaScript box/writeback、`cpp` section temporary/copy-out 和两目标 optional forwarding 均消费该计划，不再由 renderer 依据 AST section 形状重新推断调用 ABI。架构门禁禁止 call-site 回退为并行数组或 target LIR 重新携带源 `argument_intents`。
- 双目标 LIR artifact schema 升级到 v4。JavaScript LIR 固化 script/ESM、export 与 value/reference-box 函数 ABI，`cpp` LIR 固化 template parameter、value/const-reference/mutable-reference/optional-reference、具体 optional 类型和递归返回/前置声明 ABI；两个目标分别以按 `LirNodeId` 的 CSR `offsets + slots` 保存 comparison、call、section、selection、unpack 与 loop 临时资源。renderer 删除动态临时名分配、module option 和源 parameter intent 解释，目标 verifier 拒绝缺失、重复、碰撞、非稠密计划。
- 双目标 LIR artifact schema 继续升级到 v5：Program 与 Function 显式保存目标 scope/declaration plan。JavaScript lowering 一次性计算确定性声明顺序；`cpp` declaration plan 保存 concrete type 或按 `LirNodeId` 的 `decltype` probe、assignment-pattern 路径、tuple 下标、fixed-shape extent 与预解析嵌套类型。`cpp` renderer 只建立一次稠密 expression view 后 O(1) 查询 probe；两个 renderer 删除递归声明扫描。ABI/temporary/scope planner 与 verifier 从 lowering 主文件拆成目标专属编译单元，架构门禁禁止 renderer 恢复声明收集。
- 双目标 LIR artifact schema 升级到 v6。JavaScript `ModulePlan` 固化 banner、strict directive、runtime fragment 和顶层 body order；`cpp` `TranslationUnitPlan` 固化标准头文件、runtime/generated namespace、runtime fragment、前置声明/定义/入口顺序、module/entry scope 与 run/main 拓扑。两个 renderer 的公共入口删除 `TranspileOptions`，不再扫描顶层可执行语句、读取 runtime requirements 或函数图；数百行目标 runtime 模板迁入各自独立 catalog 编译单元。verifier 重建并逐字段核对布局，架构门禁阻止布局规划回流 renderer。
- 双目标 LIR artifact schema 升级到 v7，并各自新增独立 representation planner/verifier 编译单元。每个 expression 保存目标 form、precedence、token/symbol、comparison strategy、direct/custom call ABI、逐实参 value/optional-forward/reference-box/copy-section、first-result 与 index/section selector plan；`cpp` 额外保存 list concrete type、逐元素 widening、nested/matrix-linear/slice/row/column/block/N-D index 模式。两个 renderer 删除 `ExpressionKind`、`IntrinsicId`、builtin `BindingKind` 和 call-transfer 判断，只消费已验证 plan；架构门禁阻止语义决策回流。
- 双目标 LIR artifact schema 升级到 v8，新增目标专属 `StatementPlan`，覆盖当前全部 statement form、condition truthiness、value/reference-box/optional-value 参数访问、一般 N 维 JavaScript 默认数组初始化、assignment leaf、SELECT CASE selector、range/loop-else、section replacement 与函数返回；表达式计划补齐 index/slice metadata、reshape shape 和 call-result policy。representation pass 明确在 ABI/resource planning 后运行，renderer 删除 `StatementKind`、`ValueType`、assignment pattern、源 index/shape 与动态参数集合读取。
- 双目标 LIR artifact schema 升级到 v9。逐实参传递形式与 writeback 合并为单一 `CallArgumentPlan`；目标专属 `EvaluationForm`/call value/outcome 固化 comparison、lazy 和 writable-call 的 arrow IIFE/thunk 或 reference lambda、copy-in/out 与结果保存。新增按 `LirNodeId` 稠密的 `SourceSegmentPlan` 与独立共享规划工具，renderer 只按 ID 标记，不再读取节点 location/line/origin 或扫描 call argument 决定 wrapper；空表达式 no-op 在 representation 层明确降为 `discard`。
- Analyzer 按职责拆为控制/函数分析、表达式/调用/索引分析和内部 contract 三个编译单元，避免继续扩张单体源码；name/flow 表在参数关联改变结构后按新 revision 重建。
- 分析后再次检查 HIR 节点资源上限，防止默认参数物化绕过前置门禁；新增 HIR/semantic/name/flow/alias-effect dense/revision/stale、scope corruption、弱化 effect、跨函数摘要、call borrow/copy/forward/lifetime/overlap，以及目标 ABI/temporary/scope/declaration/module/translation-unit/expression/statement/evaluation/source-segment representation plan 负向测试，内部测试增至 158 项；本轮生产代码行覆盖率实测 89.58%（17561/19604），继续高于 85% 门槛。

## 0.3.4

- Analyzer 当前全部节点输出迁入按 `HirNodeId` O(1) 索引的紧凑 `SemanticTable`；表绑定 HIR revision，verifier 检查完整 ID 覆盖、origin 和关联 arity。分析结束后类型、shape、binding、call argument association、递归 assignment-pattern 路径及 flow 元数据从 HIR move-extract，MIR 对缺失/陈旧表失败关闭且不再读取 HIR 语义投影。
- 一般静态 rank 对象语义扩展到任意维常量声明、嵌套 shape、RESHAPE、直接 index/section 读取与写入。JavaScript 使用通用 column-major 坐标递归，`cpp` 使用 C++17 模板递归构造/选择/写回嵌套 vector；新增三维 Fortran tensor 由 gfortran、Node.js 和生成 C++ 实际执行的差分门禁。
- Frontend descriptor 升级到 API v5：manifest 声明 minimum/maximum language version、能力 bitset 与完整 resource-limit contract；生产驱动和 conformance 通过工厂为每次编译创建独立 parser session。公共 `LanguageVersion`、CLI `--language-version` 和 `MPF1201` 失败关闭已接入；首批 feature gate 覆盖 Python 3.8 positional-only parameter 与 Fortran 2003 bracket array constructor，并接受 Python `pass`、Fortran `CONTINUE` no-op 产生式。
- 删除职责泛化的 `.github/workflows/ci.yml`，GitHub Actions 改为 validation、跨平台/差分、质量、Sanitizer、覆盖率、性能、安全和发布独立失败域；统一最小权限、并发取消、超时、失败产物和发布 SHA-256，并将 GitHub Actions 的 Dependabot 更新合并为每周一个批次。官方 setup、artifact 与发布 Action 全部升级到 Node.js 24 运行时版本，消除 Node.js 20 退役告警。Fortran 外部 oracle 回退到 gfortran 实际支持的严格 `f2018`；安全 workflow 显式探测 GitHub Advanced Security 能力，区分订阅不可用与代码失败；修复 MSVC `/WX` 发现的生成 C++ bool/int 比较、常量步长条件和无条件 `break` 后递增不可达警告。
- `.gitignore` 补齐 CMake/Ninja、跨平台编译产物、coverage/profile/sanitizer/fuzz、Python/Node.js、IDE 和 OS 本地状态，同时保留可入库的环境示例。
- 内部测试增至 145 项、differential corpus 增至 48 个（Python 20、Fortran 18、Matlab 10），CTest 增至 59 项；工具完整环境共执行 134 条源语言/Node.js/生成 C++ 程序路径。
- 生产驱动切换为“语言 AST artifact → HIR → MIR → JavaScript LIR/`cpp` LIR → Emitter”；新增强类型 AST/HIR/MIR/LIR identity、逐层 verifier、opaque backend artifact，删除共享 `Program` 直通 emitter 的路径。
- 新增 thread-confined `CompilationSession` 基础、HIR/MIR/LIR 强类型 pass manager、revision-aware `AnalysisManager`、preserved-analysis 失效、逐 pass verifier、耗时 instrumentation，以及确定性的 HIR/MIR textual dump。
- MIR 新增稠密 type/shape/storage/instruction/function/basic-block 表和结构化 effect；当前 if/loop/loop-else/`break`/`continue`/`SELECT CASE` 生成真实 edge、block argument 与 edge actual，shape 保存 canonical stride，storage 保存 view/base/lifetime/intent 和保守 alias relation；统一 `alias_between` 为 pass 提供 fail-safe 查询。verifier 覆盖 ownership、edge arity、定义顺序、dominance 及 type/shape/storage/alias metadata。
- frontend descriptor（现为 API v5）增加语言版本范围、能力 bitset、resource contract、parser-session factory、AST schema/determinism/reentrancy manifest、语言 AST verifier 和 AST→HIR lowering；三个内置前端拥有编译期互不兼容的 PMR arena AST、稠密 `AstNodeId`、确定性 dump 和专用 AST→HIR visitor，生产 artifact 不再封装共享 syntax tree。
- backend descriptor 升级到 API v4，增加目标标准/artifact schema、完整 configuration schema、runtime license/supply-chain manifest、TargetProfile、legalization factory 和 semantic LIR dump callback。两个后端逐 MIR instruction 执行稠密 legalization，构建不借用 MIR 生命周期的私有 semantic plan、独立 LIR/pass/verifier，并在 target renderer 中完成 representation/type/shape/ABI、runtime/binding、函数依赖和名称计划；最终 emitter 只执行 `serialize_chunks`。
- 新增可复用 frontend/backend extension conformance harness；重复执行 parse/lowering/verifier/emission 并逐字节验证确定性。新增编译器分层静态门禁，禁止 frontend/公共 IR/双目标后端出现反向依赖。
- 新增公开 `ResourceLimits`，对 source bytes、token、parser depth、arena、AST/HIR/MIR/LIR 节点、生成输出和 source map 逐阶段限制，以 `MPF0010` 失败关闭；新增机器可读 `CompilationReport`，记录阶段耗时、节点数和峰值 arena。
- 新增从最终 LIR chunk origin 构建的确定性 source map v3、CLI `--source-map`、公共 dependency manifest；代码、map 与依赖形成稳定 output bundle。
- 新增三语言/双目标 corpus mutation fuzz smoke 和可选 Clang libFuzzer target，提供 crash replay/minimize 工作流；新增小文件、吞吐、深 CFG、大 shape、函数图、八路并发、峰值 arena 与产物大小 JSON 性能发布门禁，并由 CI 归档报告。
- 本轮最终生产代码行覆盖率实测 88.26%（13779/15611），高于 85% 硬门槛。
- 新增 Python/Matlab 等价语义 normalized HIR golden、JavaScript/`cpp` semantic LIR 人类可读 dump 与逐字节 golden；backend conformance 同时验证 dump 和最终代码确定性。新增两个真正通过安装包 `find_package` 构建运行的仓库外 frontend/backend consumer 模板。
- 性能基线绑定项目版本 `0.3.4`，报告版本、延迟、吞吐、峰值 arena 与生成产物大小任一不匹配均阻断发布。
- 同步架构、扩展、测试、诊断、支持矩阵和 TODO：完整官方 grammar、Analyzer 内部直接 side-table 写入、HIR/MIR 宽投影收敛、动态 N 维对象/精确 alias 语义和稳定插件 ABI 仍明确保持后续任务。

## 0.3.3

- 新增版本化 `FrontendDescriptor` 与静态 frontend registry；canonical name、alias、扩展名、无分配内容 probe 和 parser callback 归每个前端所有，核心驱动移除语言检测和解析的硬编码分派。
- 扩展 `BackendDescriptor`，统一 target identity、name/alias、intrinsic binding、capability validator 和 emitter 回调；backend registry 改为数据驱动 catalog，并为禁用目标保留可查询 metadata，生成任一目标继续不依赖另一目标产物。
- 新增稳定 `IntrinsicId`、可选共享数学表和由各 `FrontendDescriptor` 显式选择的有序 spelling 表；未声明相同全局拼写的新语言不会错误继承现有 builtin。Analyzer 与 emitter 不再用源 builtin 字符串建立隐式协议。JavaScript/`cpp` 使用稠密 O(1) 代码绑定表，区分 `symbol`、`constant`、`custom` 与 `unavailable`。
- 在目标 capability validator 前新增通用绑定完整性检查；任一已解析 intrinsic 缺少目标 binding 时以 `MPF0004` 失败关闭，不进入 emitter。
- 公共 API 新增 frontend availability、源/目标名称有效性查询；CLI 名称解析复用 registry，不再维护独立白名单。
- 新增 descriptor 冲突、扩展名/内容探测、禁用后端 metadata、源 intrinsic 隔离、双目标绑定完整性和缺失绑定拒绝测试；内部测试增至 128 项，既有 47 个差分 case 和 54 项 CTest 保持通过。
- Clang source-based coverage 实测 88.51%（9550/10790），继续高于 85% 生产代码行门槛。
- 新增前端、后端和代码绑定扩展指南，并将 TypeScript 6 独立前端纳入后续里程碑；当前 descriptor 是编译期内部 contract，尚不承诺动态插件 ABI。

## 0.3.2

- Python expression lexer/parser 新增专用 `if`/`else` token、显式 comparison-chain AST 和右结合 conditional-expression AST；非 Python 源继续拒绝链式比较。
- Analyzer 对比较链逐对验证 ordering 类型，并在条件表达式分支间传播 scalar、list shape 和 tuple metadata；不兼容 ordering 或 C++ 无法静态表示的比较/分支组合以 `MPF2044` 失败关闭。
- JavaScript 使用短路 IIFE 和临时值 lowering 比较链，C++ 使用捕获 lambda 和引用临时值；两者都保证中间操作数单次求值且后续操作数按需执行。JavaScript equality runtime 保留 Python bool/number 和当前同类递归 list 相等规则；条件表达式在两个后端均保持 Python truthiness、右结合与惰性分支。
- 新增 token/AST、成功/拒绝、双后端结构和 CPython/Node.js/生成 C++/oracle 四路差分；内部测试增至 122 项，语料增至 47 个、131 条实际执行路径。
- 按实际能力重构 TODO 与状态文档，区分已交付子集、下一交付目标和长期 backlog；明确当前生产实现为 C++17、目标身份为 `cpp`、双后端互不依赖及废弃归档边界。

## 0.3.1

- Fortran statement lexer/parser 新增结构化 `SELECT CASE`、`CASE DEFAULT` 与 `END SELECT`/`ENDSELECT`，支持 integer、character、logical scalar selector，以及单值、闭区间和省略上下界的区间列表。
- 公共 IR 新增 `select_case`、`case_clause` 和显式 `CaseSelector`；Analyzer 要求受支持的 scalar constant bound，验证 selector 类型、反向区间、integer/character/logical 重叠，并以 `MPF2043` 失败关闭。
- 控制流分析从两分支扩展到任意 CASE 分支合流；只有包含 default 且所有可达分支均赋值时，变量才被视为确定赋值，所有分支终止时也可形成完整终止流。
- JavaScript/C++ 后端分别保存 selector 临时值并生成互斥条件链，保证函数 selector 单次求值；character CASE 使用独立 runtime 比较，按 Fortran 规则将较短值在右侧补空格。
- 新增 token、成功/拒绝、双后端结构和 gfortran/Node.js/生成 C++/oracle 四路差分；内部测试增至 119 项，语料增至 46 个、128 条实际执行路径。

## 0.3.0

- 新增项目级 `.clang-format`、`mpf-format` 与只检查不改写的 `mpf-format-check`，统一公共头、源码、测试和 embedding 示例格式。
- 新增 curated `.clang-tidy` 与 `quality` preset，对 Clang analyzer、bugprone、performance 和 portability 工程规则执行全目标零告警构建；修复由门禁发现的重复分支和无效状态写入。
- 新增 Clang source-based coverage、`coverage` preset 和 `mpf-coverage` target：完整运行 52 项 CTest，合并多进程 profile，生成 HTML/JSON，并以 85% 生产代码行覆盖率阻止回退；当前基线为 87.94%（8363/9510）。
- GitHub CI 新增格式、clang-tidy 和覆盖率 job；新增 C/C++ CodeQL `security-extended` 定期/提交扫描、pull-request 依赖漏洞审查，以及 GitHub Actions Dependabot 更新。
- 质量工具和所有报告继续严格写入根目录 `build/`；本里程碑不改变 `cpp` 目标身份或 C++17 输出标准。

## 0.2.9

- 新增独立 `AssignmentPattern` IR，递归表示 name、sequence 与 starred-name target；Python parser 支持任意当前可表示深度的圆括号/方括号嵌套、单目标尾随逗号及每层一个 star。
- 表达式、Symbol 和 function return/call 新增递归 `ValueMetadata`，跨固定 tuple/list literal、静态名称和已知 user-function 传播 sequence kind、逐元素 type/shape 与嵌套结构。
- Analyzer 递归关联 pattern 与固定 RHS，计算普通叶子访问路径和 star capture 路径；支持 star 位于任意位置、空 capture、嵌套 capture 与重复名称覆盖，并以 `MPF2042` 拒绝动态或不匹配结构。
- JavaScript 不依赖原生 rest-position 限制，先保存 RHS 后按路径逐叶赋值；C++ 同样单次求值，并以 `std::get`/`.at()` 和 typed vector 构造 star list。异质 star capture 保持 JavaScript 可用，C++ 以 `MPF2020` 失败关闭。
- 新增 parser 结构、成功/拒绝、双后端 lowering 和 CPython 3.14/Node.js/生成 C++/oracle 四路差分；内部测试增至 116 项，语料增至 45 个、125 条实际执行路径。

## 0.2.8

- 将目标身份统一命名为 `cpp`；C++17 仅表示当前输出语言标准，不再进入代码标识符或组件身份。
- 公共 API 使用 `TargetLanguage::cpp`，CLI 使用 `--target cpp`，后端入口统一为 `cpp_backend`、`emit_cpp` 与 `validate_cpp_capabilities`。
- 后端源文件统一为 `cpp_backend.*`、`cpp_emitter.*`、`cpp_validator.*`；CMake 使用 `MPF_ENABLE_CPP_BACKEND`、`mpf_backend_cpp`、`mpf::backend-cpp` 和 `cpp` package component。
- 生成代码编译门禁、差分结果字段、隔离构建目录/标签与外部消费者宏全部同步为 `cpp`，避免 API、构建系统和测试出现两套身份。
- 本次为 0.x 公共命名清理，不保留带标准版本号的旧标识符别名；生成 translation unit 仍以 C++17 严格编译。

## 0.2.7

- Python tokenized statement parser 新增平坦名称解包 target，覆盖裸 `a, b`、圆括号、方括号和单目标尾随逗号；nested/starred pattern 在对应 pattern/iterator 模型完成前以 `MPF1200` 失败关闭。
- Analyzer 将固定 tuple/list literal、带静态首维 extent 的 list 名称和已知 tuple-return user function 规范化为逐 target type/element-type/shape 元数据，并以新增 `MPF2042` 拒绝动态未知长度或数量不匹配。
- Symbol 与跨函数 return/call 元数据保留 tuple 元素信息，支持 tuple-return forwarding、tuple/list 名称解包、异质 tuple、交换赋值和 Python 合法的重复目标覆盖顺序。
- JavaScript 使用原生 destructuring；C++17 先保存 RHS 临时值，再分别用 `std::get` 或 bounds-checked `.at()` 赋值，保证函数调用和交换 RHS 单次求值，且两个后端继续直接消费同一目标无关 IR。
- 新增 parser、成功/拒绝和双后端结构测试，以及 CPython 3.14、Node.js、生成 C++17、oracle 四路 unpacking 差分；语料增至 44 个，实际执行 122 条程序输出路径并逐 case 对照 oracle。

## 0.2.6

- Procedure IR 新增逐参数 `ParameterKind` 与 default expression AST；Python call expression 复用 keyword actual 元数据，前端不拼接目标代码。
- Python tokenized function parser 支持 immutable scalar defaults、`/` positional-only marker、裸 `*` keyword-only marker、尾随逗号与 required keyword-only parameter，并拒绝重复、顺序错误、annotation 和 variadic 参数。
- Analyzer 按已知 user-function signature 规范化 positional/keyword actual、补全默认值，以 `MPF2034`/`MPF2041` 拒绝过多、重复、未知、缺失、positional-only-by-keyword 和 keyword-only-by-position。
- 默认表达式当前只接受无副作用 immutable scalar literal 与一元正负号；list/call/identifier 等在 Python 定义时求值与对象身份模型完成前失败关闭。
- JavaScript 保留可读 default signature，JavaScript/C++17 的所有 source call 均从同一 formal-order IR 生成；新增 CPython 3.14、Node.js、生成 C++17、oracle 四路 parameter-association 差分，语料增至 43 个、127 条路径。

## 0.2.5

- Fortran optional formal 从标量 IN 扩展到当前一/二维范围内的标量与数组 IN/OUT/INOUT；`PRESENT` 对 OUT dummy 不触发未初始化读取，缺省 optional OUT 不再错误要求无条件确定赋值。
- JavaScript reference-call lowering 区分 omitted actual、普通 writable actual 和 optional-to-optional 透传，缺省值保持 `undefined`，存在值使用 box 并按 element/section/whole-array 目标有序回写。
- C++17 runtime 新增 typed `optional_argument<T>`，可保存 absent、外部 lvalue reference 或 owned rvalue，并在复制/移动及跨 procedure 透传时保持引用身份；数组 formal 使用递归 `std::vector` 具体类型。
- optional writable actual 支持标量名、数组元素、整组一/二维数组和连续/非连续 section；section 继续通过单次调用 lambda 与 typed copy-out 保持语义。
- 新增双后端结构测试和 gfortran、Node.js、生成 C++17、oracle 四路 optional-writeback 差分；语料增至 42 个、123 条路径。

## 0.2.4

- 表达式 token/AST 新增 Fortran keyword actual 名称；Procedure IR 新增逐 formal optional 元数据，并以目标无关 omitted-argument 节点表示缺省关联。
- Fortran declaration parser 支持组合及换序的 `INTENT`/`OPTIONAL` attributes；Analyzer 根据已知 interface 规范化 positional/keyword actual，验证未知 keyword、重复 association、缺失 required 和 positional-after-keyword。
- 支持标量 `OPTIONAL, INTENT(IN)`、`PRESENT`，以及 optional dummy 向 optional dummy 透传；optional array 和 writable optional 当前以 `MPF2040` 失败关闭。
- JavaScript 独立 lowering 为 `undefined` 与存在性判断；C++17 独立 lowering 为具体类型 `std::optional<T>`、`std::nullopt`、`has_value()`/`value()`，不依赖 JavaScript 生成结果。
- 新增 lexer/Pratt、成功与拒绝集成测试，以及 gfortran、Node.js、生成 C++17、oracle 四路 argument-association 差分；语料增至 41 个、119 条路径。

## 0.2.3

- 中立调用 IR 新增 procedure-result 标记，并延续逐参数 intent；Analyzer 将可写 actual 扩展为标量名、数组元素和直接一/二维 section，同时以根 storage 为单位保守拒绝潜在写回 alias。
- 标量元素在 C++17 中直接绑定 element reference；JavaScript reference box 在调用后通过安全 `__mpf_set` 回写。
- 连续与非连续 section actual 使用显式 copy-in/copy-out：JavaScript 复用 selector-aware `__mpf_set_section`，C++17 生成 typed section 临时值、单次调用 lambda 与 `assign_*` 回写。
- 带可写 section 的 Fortran function 调用先保存返回结果、执行 copy-out，再返回结果，避免 mutation 与函数值互相丢失；SUBROUTINE 保持 void 调用。
- 新增成功、潜在 section alias 拒绝与双后端结构测试，以及 gfortran、Node.js、生成 C++17、oracle 四路 section-reference-arguments 差分；语料增至 40 个、115 条路径。

## 0.2.2

- Procedure IR 增加逐参数 type、element type 与 shape 元数据；Fortran parser 支持一/二维 assumed-shape dummy `(:)`/`(:,:)`，并标记 dummy declaration 避免后端错误分配 dynamic extent。
- Analyzer 验证 dummy/actual 的 scalar-array 分类、rank、静态 extent 与 element type；非 dummy assumed-shape 以新增 `MPF2039` 失败关闭。
- C++17 使用 const/reference 模板参数共享一/二维递归 vector storage；JavaScript OUT 数组 box 保留 actual container，并通过 `.value` 执行 indexed mutation 和写回。
- 非连续 section actual 在 copy-in/copy-out lowering 完成前继续以 `MPF2038` 拒绝，避免把临时 section 当作可写整数组。
- 新增成功、rank/extent/section 拒绝测试，以及 gfortran、Node.js、生成 C++17、oracle 四路 array-reference-arguments 差分；语料增至 39 个、111 条路径。

## 0.2.1

- 中立 IR 新增 `ParameterIntent`、procedure parameter intents 与调用表达式 argument intents，前端/Analyzer 不包含目标后端策略。
- Fortran declaration parser 支持 `INTENT(OUT/INOUT)`；Analyzer 从 dummy 声明和实际读写推导 IN/OUT/INOUT，保留 OUT 入参未定义状态并验证所有退出路径确定赋值。
- 调用分析要求 OUT/INOUT actual 为可定义名称，允许未初始化 OUT、要求 INOUT 已赋值，拒绝多写回参数绑定同一 actual，并把调用后的 actual 标记为已赋值。
- C++17 生成 `const T&`/`T&` procedure 参数；JavaScript 生成独立 reference box、单次调用 IIFE、函数结果保存及有序 actual 写回，支持跨 procedure 透传。
- 新增 `MPF2038`、成功/拒绝集成测试，以及 gfortran、Node.js、生成 C++17、oracle 四路 reference-arguments 差分；语料增至 38 个、107 条路径。

## 0.2.0

- Fortran statement lexer 新增 `FUNCTION`、`SUBROUTINE`、`RESULT`、`RETURN`、`RECURSIVE` token，同时在实体名位置保持关键字上下文化。
- Fortran recursive parser 支持 program 内部/外部 typed/untyped function、result variable、subroutine、dummy parameter list、`INTENT(IN)`、`CALL`（含无参数形式）、提前 `RETURN` 与具名 `END` 校验。
- Analyzer 保留 typed function result seed、dummy declaration 的确定赋值状态，并把 procedure 接入共享函数调用图；递归标量 function 与递归 void subroutine 均可生成。
- 对 dummy argument 初始化、标量写入、indexed/section 写入和作为 DO variable 的修改新增 `MPF2036`，在 reference-argument lowering 完成前禁止错误的按值降级。
- IR 显式记录 Fortran CALL statement 上下文；Analyzer 以 `MPF2037` 拒绝 CALL function 或在表达式中引用 subroutine，并要求显式 `RECURSIVE FUNCTION` 使用 `RESULT` 消除结果名歧义。
- 新增 procedure token、成功/拒绝/contextual-name 测试，以及 gfortran 2023、Node.js、生成 C++17、oracle 四路 procedure 差分；语料增至 37 个、103 条路径。

## 0.1.9

- 新增目标无关 `FunctionDependencyGraph`，从结构化表达式/statement IR 收集调用边，排除参数、结果和局部绑定造成的伪依赖，并稳定识别直接/互递归分量。
- Analyzer 改为 callee-first 分析顶层函数，使后定义函数的返回类型、元素类型、shape 与 Matlab 多输出元数据可跨调用链传播。
- C++17 emitter 按同一依赖顺序定义无环函数；对静态标量或 tuple 返回的递归函数生成显式返回类型和模板前置声明，支持直接及互递归。
- C++17 capability validator 新增 `MPF2035`，对未知、容器或参数依赖且无法建立合法 C++17 声明的递归返回失败关闭；JavaScript 后端保持独立可用。
- 新增局部遮蔽调用图单元测试、前向/直接/互递归集成测试、Matlab 多输出跨函数转发测试，以及 Python/Matlab function-graph 差分语料；语料增至 36 个、99 条路径。

## 0.1.8

- 中立 IR 新增多目标赋值、调用方请求输出数及逐输出 type/element-type/shape 元数据；该语义不引用 JavaScript 或 C++17 后端。
- Matlab parser 支持 `[a,b] = f(...)`，Analyzer 先完成顶层/local function 元数据，再分析脚本调用，并验证输入数量、输出数量、重复目标及非多输出 RHS。
- Matlab 多输出函数在普通标量上下文按语言规则选择第一个输出；JavaScript 独立 lowering 为 Array 首元素或解构，C++17 独立 lowering 为 `std::tuple/std::get`。
- 多目标赋值在两个后端都保证 RHS 只求值一次；C++17 capability validator 同时检查各绑定的静态类型变化。
- 新增成功、拒绝与严格生成代码测试，以及 Matlab multi-output 双目标差分；语料增至 34 个、93 条路径。

## 0.1.7

- 新增 Fortran statement lexer，基于共享 token/span 载体分类 program、声明、IF/DO、I/O、delimiter、`::`、legacy constructor 和 dotted operator 边界。
- 新增递归下降 Fortran statement parser，覆盖当前 program scaffolding、类型声明、常量一/二维 shape、`IF/ELSE IF/ELSE`、`DO/DO WHILE`、EXIT/CYCLE、PRINT/WRITE、CALL 与赋值子集。
- Fortran frontend 改为 source-form normalizer → statement lexer → statement parser 三阶段管线；三语言 statement 路径现均不再依赖 regex/prefix 行解析。
- Fortran 名称按上下文解释，修复 `block` 等合法实体名被全局关键字表误拒绝；未建模 declaration attributes 现在显式失败关闭。
- 修复标准无逗号 `WRITE(*,*) value` 产生式，同时兼容常见带逗号形式。
- 新增 `MPF1801`/`MPF1802`、lexer/parser 恢复与拒绝测试，以及 gfortran 四路 statement-token 差分；语料增至 33 个、91 条路径。

## 0.1.6

- 抽取共享 `BasicStatementToken<Kind>`，统一 Python/Matlab statement token 的文本、byte span 与源位置布局。
- 新增 Matlab statement lexer，分类函数/控制流关键字、delimiter、赋值与运算符，并区分字符向量、共轭转置和非共轭转置边界。
- 新增递归下降 Matlab statement parser，覆盖当前函数/多输出签名、`if/elseif/else`、`while/for`、循环控制、display、赋值、索引赋值和表达式语句子集。
- Matlab frontend 改为 logical-source normalizer → statement lexer → statement parser 三阶段管线，移除 regex/prefix parsing 和独立 colon 字符扫描。
- Analyzer 将 Matlab 单输出变量类型传播到函数返回类型，多输出函数标记为 tuple，修复 C++17 全局调用结果错误回退到声明前 `decltype(call)`。
- 新增 `MPF1701`/`MPF1702`、parser 恢复/拒绝测试和 Matlab statement-token 双目标差分；语料增至 32 个、88 条路径。

## 0.1.5

- 新增独立 Python statement token/span 模型和 lexer，保留关键字、delimiter、赋值与运算符边界及源位置。
- 新增递归下降 Python statement parser，覆盖当前 `def`、`if/elif/else`、`while/for-else`、return、循环控制、赋值、索引赋值、print 与表达式语句子集。
- Python frontend 现为 logical-source normalizer → statement lexer → statement parser 三阶段管线；移除全部 regex/prefix statement parsing。
- statement parser 通过 token byte span 把表达式交给共享 Pratt parser，避免维护第二套表达式语法。
- 增加非法链式赋值、参数形态、孤立 clause 恢复测试，以及 `MPF1601`/`MPF1602` statement lexer 诊断。
- 新增 CPython 3.14、Node.js、生成 C++17 的 statement-token differential case；语料增至 31 个、86 条路径。

## 0.1.4

- 新增独立 Python/Matlab logical-source normalization 层，两个 statement parser 不再直接消费物理行。
- Python 支持括号内隐式续行、反斜杠显式续行、字符串安全行内注释、tab-stop 缩进及顶层分号 simple statements。
- Matlab 支持 `...` continuation、跨物理行矩阵、字符串安全 `%` 注释、`%{`/`%}` block comment 及顶层分号/逗号 statements。
- 为 Python 增加 `MPF1401`—`MPF1405`，为 Matlab 增加 `MPF1501`—`MPF1505`，对未闭合 delimiter/string/comment 和错误 continuation 失败关闭。
- 移除 Matlab statement parser 中重复的注释/尾分号字符扫描，物理源码规则统一位于前端规范化边界。
- 新增 Python 四路与 Matlab 双目标 logical-lines differential case；语料总数增至 30 个、83 条执行路径。

## 0.1.3

- Python `if`/`while`/`not` 增加数字、字符串、list、`None` truthiness runtime；NaN 按 Python 规则为真。
- Python `and/or` 不再错误推导为 bool，现保留操作数返回、短路顺序和左操作数单次求值。
- JavaScript backend 使用 lazy thunk lowering；C++17 backend 使用 typed lambda 与 `std::common_type` runtime。
- C++17 capability validator 解析已知函数返回类型；无法静态统一逻辑结果时以 `MPF2032` 提前失败。
- 增加 Python `float` 的数字/布尔/字符串基础转换、NaN/Infinity 解析和 `MPF2033` 参数诊断。
- C++17 list 变量优先使用语义 element/rank 声明，避免在 namespace `decltype` 中生成 C++17 禁止的 lambda。
- 新增四路 truthiness differential case，覆盖空/非空容器和字符串、NaN、`not`、list/string 返回及短路副作用。

## 0.1.2

- 用单一 declarative corpus manifest 替换分散的源 Python/Fortran、Node.js 和生成 C++17 CTest 注册。
- 新增跨平台 differential runner；每个 case 直接比较所有可用执行路径，并再次校验 oracle。
- 27 个 case 覆盖 12 个 Python、9 个 Fortran、6 个 Matlab 程序，共执行 75 条源/目标路径。
- 生成 C++17 使用与顶层构建相同的 compiler、generator、platform 和 toolset，避免 Clang job 实际回退到默认 GCC。
- 每个 case 保存生成 JS/C++、嵌套严格构建和包含工具/结果的 `differential-result.txt`。
- CI 固定 Python 3.14 与 Node.js 24，强制 differential runtime 存在，并始终上传差分制品。
- 保留 Matlab function translation unit 的独立 C++17 compile-only gate；删除三个重复旧 runner。

## 0.1.1

- 新增独立 Fortran source-form normalization 层，statement parser 和两个 emitter 不再处理物理续行细节。
- free form 支持尾部/前导 `&`、continuation 间注释、续行字符常量和分号 logical statements。
- fixed form 支持列 1–5 label field、列 6 continuation、列 7–72 statement field及传统整行注释。
- 通过 `.f`/`.for`/`.ftn`/`.f77` 与现代扩展名自动选择 source form；公共 API 增加 `FortranSourceForm`。
- CLI 增加 `--fortran-form auto|free|fixed`，显式 source form 可令 stdin 自动识别为 Fortran。
- 增加 `MPF1301`—`MPF1307` 稳定诊断，拒绝孤立/未完成 continuation、预处理、错误字符续行和不安全 fixed-form 布局。
- 新增 free/fixed corpus，并由 gfortran、Node.js 和真实生成 C++17 三方执行验证。

## 0.1.0

- 增加拥有稳定 `SourceId`、文件名查找和稳定源码引用的多文件 SourceManager，并接入每次转译会话。
- 公共 `Diagnostic` 增加 source identity 与结束位置；所有 parser、semantic 和 backend 诊断统一附着源文件。
- 公共 API 增加确定性的源码片段文本渲染与 JSON diagnostics v1 序列化。
- `mpfc` 增加 `--diagnostics-format text|json`；JSON 模式每次调用恰好产生一个 schema 文档。
- 固定 CLI 退出状态：0 成功、1 编译错误、2 参数错误、3 输入错误、4 输出错误，并增加 `MPFCLI0001`—`MPFCLI0003` 驱动诊断。
- 新增跨平台 CLI 契约测试，覆盖文本片段、JSON 编译/驱动错误、成功空数组及输出失败事务。
- 安装 diagnostics v1 JSON Schema，并新增诊断与工具集成文档。

## 0.0.9

- Python 增加一维 list 普通切片可变长度替换及 extended slice 等长赋值；动态长度不相容由运行时拒绝。
- Matlab 增加行、列、矩形 block 与列主序线性 colon section 赋值，并支持标量扩展。
- Fortran 增加一/二维 array section 赋值、shape conformability 验证和标量扩展。
- 目标无关语义层增加 section replacement shape/元素类型分析和稳定诊断 `MPF2031`，并拒绝经临时 section 写入。
- JavaScript backend 增加原位 selector mutation runtime；C++17 backend 增加 typed slice、column、block 与列主序 mutation runtime。
- C++17 capability validator 独立拒绝 Python 容器嵌套 rank 或元素类型变化，保持 JavaScript 动态语义不受影响。
- 新增三语言赋值语料，覆盖源 Python/Fortran、Node.js 与真实生成 C++17 编译执行。

## 0.0.8

- 将单体 `libmpf` 拆分为 `mpf-core`、`backend-common`、JavaScript backend、C++17 backend 与统一 facade。
- 增加 JavaScript/C++ 后端独立开关，禁用后端不会参与编译或链接。
- 引入 backend descriptor/registry；facade 不再直接包含或调用具体 emitter。
- 通用语义分析移除 `TargetLanguage`，C++17 动态类型、同质容器和返回路径限制迁入独立 capability validator。
- 增加 JavaScript capability validator 边界，为后续目标特定规则提供独立扩展点。
- 公共 API 增加 `backend_available`；请求未构建后端返回 `MPF0003`。
- CMake 包增加 `core`、`javascript`、`cpp` 组件和独立导出目标。
- 新增 javascript-only、cpp-only、core-only 的编译数据库隔离、CLI、安装包和外部消费者测试。
- 修复 Python ragged list 在目标无关分析中丢失 rank 的问题，以动态 extent 保留安全索引信息。

## 0.0.7

- 增加结构化 slice AST，统一 Python `start:stop:step`、Matlab colon 与 Fortran subscript triplet。
- 语义层增加静态/动态 slice extent、正负 step、空 extent、逐维 bounds 与固定 shape assignment 验证。
- Python 增加默认/负步长读取切片，并将矩形嵌套 list shape 推导扩展到任意深度。
- Matlab 增加整行、整列、二维 block、步长 colon 与 `A(:)` 列主序线性选取。
- Fortran 增加一/二维 array section、默认 bound 和正负 stride。
- JavaScript 增加通用 selector section runtime；C++17 增加 typed slice/column/columns/column-major flatten runtime。
- C++17 推导声明使用 `std::decay_t`，避免子数组索引结果泄漏引用类型。
- 新增三语言 section 和 Python tensor corpus，覆盖 Node.js、真实 C++17、CPython 与 gfortran 执行路径。

## 0.0.6

- 表达式与语义 IR 增加矩形二维 shape、多下标目标和列主序元数据。
- Python 增加矩形嵌套 list 与链式二维索引读写；C++17 对 ragged list 安全拒绝。
- Matlab 增加分号分行矩阵字面量、`(row, column)` 访问和列主序线性索引。
- Fortran 增加二维常量 extent、rank 检查及一/二维 `RESHAPE`。
- JavaScript runtime 增加多下标读写、递归 `sum`/`numel`、二维 `length` 和列主序 reshape。
- C++17 后端增加递归 `std::vector` 类型/分配、深度聚合、二维安全索引和列主序 reshape。
- 新增 Python、Matlab、Fortran 二维语料，并在 Node.js、真实 C++17、CPython 与 gfortran 路径执行验证。

## 0.0.5

- 增加结构化 index AST、indexed assignment IR、element type 与一维 shape。
- 增加 Python list 索引读写、负下标、`len` 和 `sum`。
- 增加 Matlab 逗号/空格行向量、1-based 索引、`length`/`numel` 和 `sum`。
- 增加 Fortran 一维定长数组、现代/旧式构造器、1-based 索引、`SIZE` 和 `SUM`。
- 增加静态 extent 匹配、同质元素约束、常量越界和索引类型诊断。
- JavaScript 与 C++17 后端增加 bounds/base/negative-index runtime；C++17 使用类型化 `std::vector`。
- 数组 corpus 纳入 Python 3.14、gfortran 严格 reference mode、Node.js 与真实 C++17 编译执行测试。

## 0.0.4

- 增加 Python `elif`、Matlab `elseif` 与 Fortran `ELSE IF` 分支链。
- 增加 Python/Matlab `break`/`continue` 与 Fortran `EXIT`/`CYCLE`。
- 增加 Python `for/while ... else`，通过每层独立完成标志保持嵌套 break 语义。
- 增加循环/函数上下文验证、不可达代码 warning 和基础终止流摘要。
- C++17 后端拒绝不兼容返回类型及值返回与隐式空返回混用。
- 增加源 Python/Fortran、Node.js、生成 C++17 的结构化控制流三方执行语料。

## 0.0.3

- 增加名称绑定、builtin 遮蔽、未定义标识符和确定赋值分析。
- 增加整数、实数、布尔、字符串等基础类型推导和 C++17 动态重赋值诊断。
- 增加 Python `range`/`while`、Matlab colon `for`/`while`、Fortran counted `DO`/`DO WHILE`。
- 保持 Python、Matlab、Fortran 各自不同的循环结束变量语义并支持负 step。
- 增加 JavaScript/C++17 共享的确定性保留字与冲突安全名称改写。
- C++17 生成代码隔离到 `mpf_generated` namespace，并支持函数捕获已初始化全局值。
- 将三语言循环、名称改写和全局绑定纳入 Node.js 与真实 C++17 编译执行测试。

## 0.0.2

- 增加 JavaScript/C++17 双目标公共 API 与 `mpfc --target`。
- 增加独立 C++17 后端、基础 runtime、函数模板与可执行入口生成。
- 增加 SourceManager、UTF-8 列映射、源码跨度和 CRLF 处理。
- 增加公共 token 模型及 Python、Matlab、Fortran 表达式词法规则。
- 用 Pratt parser 和结构化表达式 AST 替换字符串表达式改写。
- JavaScript 后端增加优先级安全输出、Python floor division 和 list/tuple lowering。
- 增加生成 JavaScript 的 Node.js 验证，以及生成 C++17 的真实编译/执行测试。

## 0.0.1

- 建立公共 API、CLI、统一初始 IR、三语言标量纵切面、JavaScript 后端、测试和 CI/CD 基础。
