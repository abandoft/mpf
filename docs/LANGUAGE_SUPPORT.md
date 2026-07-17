# 语言支持矩阵

本表描述已由自动测试保证的当前语言子集，不是目标版本的完整兼容声明。

TypeScript frontend 已注册，当前 manifest 范围为 1.0—6.0，并有 Node.js 24 source/生成 JavaScript/生成 C++17/声明式 oracle 四路差分；这只声明下表中已验证的 typed/lexical-block/canonical-for 子集，不表示完整 TypeScript 6 grammar 兼容。

本表只描述语言行为覆盖，不以内部架构工作代替语言兼容声明。四个 statement parser 已直接构造编译期互不兼容的语言专属 PMR arena AST，不再经过共享 syntax tree；MIR 以强类型 ID 连接 flat expression/operation arena，每个节点绑定 resident instruction，非结构事实进入 revision-bound attribute table，直接 memory operation 另有按 `InstructionId` 稠密的 storage/root/region/mode 访问行。conditional/短路/comparison chain 与 TypeScript canonical `for` 已由 MIR CFG 固定求值/控制边，变量与 writable section call 使用显式 load/store/copy/writeback。TypeScript lexical scope 由 `NameScopeEdges`、Analyzer block state、`SymbolId` target identity 和 LIR v12 `ScopePlan` 贯通，不依赖 emitter 猜测 brace binding；explicit export policy 同样通过 semantic side table、MIR function 与 JavaScript LIR ABI 传递。后续生产路径先经过共享保守 MIR 默认优化，再基于最终 revision 计算独立 alias/effect，将跨函数参数读写按 actual region 实例化；`MemoryDependenceTable` v1 随后在函数 CFG 上建立 region-refined RAW/WAR/WAW、unknown barrier 与 loop-carried facts，最后才进入目标 semantic/rendered LIR→纯 emitter。整数常量折叠只覆盖 `int64` 与 ECMAScript safe-integer 的共同精确域，内存依赖层也只提供后续优化所需证明，不扩大任何源语言 grammar 或数值兼容声明。静态一般 rank 的声明、RESHAPE、直接 section、borrow/copy/writeback，以及同一已知 storage root 上的 element/连续/步长/N 维矩形 selector overlap 证明已完成；尚未覆盖的官方 grammar、动态 rank/广播、动态 extent、跨一般 pointer/view 的 storage association/region composition、完整 MemorySSA 与跨函数对象语义仍见 [商业级编译器管线方案](COMPILER_PIPELINE.md) 和 [TODO](../TODO.md)。

表中的“支持”表示当前子集已经进入语义/后端测试；可执行能力还应进入差分 corpus。只在某个目标可表示的结构必须由另一个目标的 capability validator 明确拒绝，不能静默改变语义。

| 能力 | Python | Matlab | Fortran | TypeScript |
|---|---|---|---|---|
| 源码形态 | 缩进式 logical statements；括号隐式续行、`\` 显式续行、行内注释、tab 展开、顶层分号；statement token/span stream | script logical statements；`...`、多行矩阵、行/block comment、顶层分号/逗号；statement token/span stream | free/fixed form 自动或显式选择；`&`/第 6 列 continuation、注释、分号 logical statements；statement token/span stream | 全源 brace/semicolon/newline token stream；`//`/`/* */` comment；`.ts`/`.mts`/`.cts` |
| 标量数字、字符串、布尔值 | 基础支持 | 基础支持 | 基础支持 | `number`、`string`、`boolean` 与 `null` literal 基础支持 |
| 标量算术与比较 | 支持；equality/ordering/identity/membership 比较链短路且各操作数最多求值一次；bool/number 数值相等，同类 list/tuple 递归相等，list/tuple 跨种类不相等；`None`/布尔 singleton 与 JavaScript sequence reference identity；string/list/tuple membership | 支持，`^` 转为 `**` | 支持，含点式比较/逻辑运算符 | 标量算术、`===`/`!==` 与 ordering；loose equality 和对象/array reference comparison 失败关闭 |
| truthiness 与逻辑 | 数字、字符串、list、`None`；`not`；operand-returning、短路且单次求值的 `and/or` | 标量逻辑基础支持 | logical `.not./.and./.or.` 基础支持 | 当前 `!`/`&&`/`||` 与控制条件限 boolean，避免两目标对象 truthiness 分歧 |
| 标量转换 | `float` 的数字/布尔/字符串基础转换，含 NaN/Infinity | 尚未系统建模 | 声明赋值的基础转换 | 尚未提供 `Number`/`String`/`Boolean` source binding |
| 标量赋值 | 支持；固定 tuple/list 解包支持递归圆括号/方括号 pattern，每层一个 starred target | 支持 | 支持，声明初始化亦支持 | typed/untyped `let`/`const`；brace block 内可遮蔽，赋值解析最近 binding；拒绝未声明赋值和 const rebinding |
| 输出 | `print(...)` | `disp(...)`/`display(...)` | `print *,`、`write(*,*)` | `console.log(...)` |
| 条件/选择 | `if`/`elif`/`else`；右结合、惰性条件表达式 | `if`/`elseif`/`else`/`end` | 块 `IF`/`ELSE IF`/`ELSE`/`END IF`；integer/character/logical `SELECT CASE`，含单值、范围与 default | braced `if`/`else if`/`else`；条件表达式尚未支持 |
| 函数 | 基础 `def`、参数、`return`；不可变标量默认值、keyword actual、`/` positional-only、裸 `*` keyword-only；前向调用；静态可表示返回的直接/互递归 | 基础文件级 local `function`；前向调用；单/多输出及跨函数转发；`[a,b]=f(...)` 单次调用；标量上下文选择首输出 | 基础 internal/external `FUNCTION`/`SUBROUTINE`、类型前缀、`RESULT`、`CALL`、`RETURN`、`RECURSIVE` 与 `INTENT(IN/OUT/INOUT)`；默认 intent；标量、整数组、数组元素及一/二维 section actual 写回；基础 keyword association；标量及一/二维数组 `OPTIONAL` 的 IN/OUT/INOUT、`PRESENT`、缺省调用与 optional 透传；前向/递归调用 | module-level `function`、参数/返回 annotation 擦除、不可变标量 default、值 `return`、前向调用；`export function` 保留显式 ESM export；nested function 失败关闭 |
| 常用数学 intrinsic | 映射到目标数学库 | 映射到目标数学库 | 映射到目标数学库 | 尚未声明全局 source intrinsic；不把 `Math.*` 错认作共享拼写 |
| 容器字面量 | 任意深度矩形嵌套 list | 逗号/空格列、分号分行矩阵；`reshape` 接受任意已知 rank 源和非空维度列表 | `[...]`、旧式 `(/.../)`；任意已知 rank 源/目标 `RESHAPE` | homogeneous array literal 与 `number[]`/`string[]`/`boolean[]` annotation |
| 标量索引读取/写入 | 0-based，支持负下标与任意深度链式索引 | 1-based 行列索引；二维线性索引为列主序 | 1-based；引用 rank 必须匹配声明 rank | 0-based array read/write；const container element mutation 可用；当前只接受可证明的整数常量 `number` index，负下标不作 Python 归一化 |
| section/slice 读取 | `start:stop:step`，缺省 bound、负 step、exclusive stop/clamp；直接多 selector 支持任意静态 rank | `:`, `start:stop`, `start:step:stop`；行/列/block/线性选取及任意静态 rank 直接 selector | `lower:upper:stride`；任意静态 rank 直接 section、缺省 bound、负 stride | 尚未支持 |
| section/slice 写入 | 一维 list 普通切片可变长度替换；extended slice 等长替换；固定 shape 多 selector 写入 | 行、列、block、列主序线性 colon 及任意静态 rank 直接 section 赋值；支持标量扩展 | 任意静态 rank 直接 array section 赋值；支持标量扩展 | 尚未支持 |
| 长度与求和 | `len`、一维/行 `sum` | `length`/`numel`；`sum` 当前限一维 | `SIZE`、`SUM` 全元素归约 | `.length`/标准库 binding 尚未支持 |
| shape | 任意深度矩形字面量、任意 rank 静态/动态 slice extent | 矩阵、任意 rank `RESHAPE` 及直接 section 的静态/动态 extent | 任意 rank 常量/assumed-shape 声明、`RESHAPE` 与直接 section shape | 当前 array literal 静态 shape；动态对象/tuple/union 尚未建模 |
| while | 基础支持 | 基础支持 | `DO WHILE` 基础支持 | braced `while` 基础支持 |
| 计数循环 | `for ... in range(...)` | `for i=start:step:stop` | counted `DO` | canonical `for (let i = init; boolean-condition; update)`；支持 `++`/`--`/`+=`/`-=`/直接赋值，尚未支持 `for of`/`for in` |
| 循环变量结束语义 | 保留最后一次迭代值 | 保留最后一次迭代值 | 保留终止增量后的值 | `let` induction binding 只在 loop statement/body scope 内，退出后不可见 |
| 循环控制 | `break`/`continue` | `break`/`continue` | `EXIT`/`CYCLE` | `break`/`continue` |
| 正常结束分支 | `for/while ... else` | 不适用 | 不适用 | 不适用 |
| 推导式/并行循环 | 尚未支持 | `parfor` 尚未支持 | `DO CONCURRENT` 尚未支持 | 尚未支持 |
| 类、模块、包 | 尚未支持 | 尚未支持 | 尚未支持 | 显式函数 export；import/export variable、class/interface/type/namespace 尚未支持 |
| 异步、并行、协程 | 尚未支持 | 尚未支持 | 尚未支持 | async/await 尚未支持 |

## 版本目标

- Python：以 3.14 grammar 和语言参考为上限，并为旧版本保留语法模式。
- Matlab：以 R2024a/R2024b 语法和核心语言语义为目标；工具箱 API 单独建模。
- Fortran：以 2023 标准为上限，同时覆盖常见 fixed/free form 历史源码。
- TypeScript：以 6.x grammar、类型擦除和 ECMAScript 运行时语义为目标；当前 manifest 上限为 6.0，已实现上表首个 typed 子集。

Fortran source form 默认由扩展名决定：`.f`、`.for`、`.ftn`、`.f77` 使用 fixed form，现代扩展名使用 free form；API 的 `FortranSourceForm` 或 CLI `--fortran-form` 可覆盖。fixed form 遵循列 1–5 label、列 6 continuation、列 7–72 statement field；当前明确拒绝 tab-form。预处理与 `INCLUDE` 尚未纳入此层。

完整覆盖将按“lexer/parser → 名称绑定 → 类型/shape 推导 → 语义 lowering → runtime”推进。每个矩阵条目只有在语义测试和差分执行测试落地后才能标记为支持。

TypeScript parser 直接从全源 token stream 构造 `typescript::ast`，支持 scalar/one-dimensional array annotation、单 declarator `let`/`const`、module-level function/default/value return、braced branch/while/canonical-for、break/continue、strict comparison 和 `console.log`。`ScopeModel::lexical_blocks` 使每个 control body、alternative 和 `for` induction/body 拥有独立 scope；同名 block local 可遮蔽不同类型，普通赋值解析最近祖先 binding，离开 scope 后名称不可见，`for (let i = i; ...)` 按 TDZ 解析为尚未初始化的新 induction binding。annotation 进入 semantic seed 后按目标表示擦除；`number` 统一为 ECMAScript 实数逻辑类型，数组索引当前只接受可证明为目标整数范围内的常量，避免 C++ 静默截断动态小数。默认值在 Analyzer 中补齐调用实参，JavaScript 同时保留参数 default；MIR verifier 明确允许 integer/real 数值相容而仍拒绝非数值签名不匹配。`export function` 通过 explicit-only export profile、statement fact、MIR function 与 JavaScript LIR ABI 保留，未标记函数不会被 ESM 自动导出。当前不支持 `.tsx`、automatic semicolon insertion 的完整规则、nested function、object/class/generic/union、arrow/template、optional-without-default、loose equality、`var` hoisting、`for of`/`for in`、import 或完整宿主标准库；这些结构以 `MPF1200`/`MPF19xx` 或后续语义诊断失败关闭。

Python parameter parser 记录 positional-only、positional-or-keyword 与 keyword-only 种类以及逐参数 default AST。Analyzer 依据已知 user-function signature 将 positional/keyword actual 规范化为 formal 顺序、补入默认值，并以 `MPF2034`/`MPF2041` 拒绝重复、未知、缺失或种类不匹配。当前默认值限定为无副作用的不可变标量 literal（含一元正负号），从而保证插入调用时不会破坏 Python 定义时单次求值语义；可变默认值、调用表达式、annotation、`*args` 与 `**kwargs` 尚未支持。

Python assignment parser 将裸/圆括号/方括号 target 解析为独立递归 `AssignmentPattern`，支持 nested sequence、单目标尾随逗号以及每层最多一个 starred name。Analyzer 接受固定 tuple/list literal、带可靠静态 extent 的 list 名称，以及逐层传播元数据的已知 user-function sequence return；它递归计算每个叶子的 type/element-type/shape 与访问路径，并以 `MPF2042` 拒绝数量不匹配、多个同层 star、非 sequence 嵌套值或动态未知长度。重复名称按 Python 从左到右覆盖；star capture 始终形成 list，C++ 对异质 capture 以 `MPF2020` 失败关闭，JavaScript 保持动态可用。字符串、一般 iterable、运行时未知长度与属性/下标 target 尚未进入当前解包子集。

Python comparison chain 当前覆盖 `<`、`<=`、`>`、`>=`、`==`、`!=`、`is`/`is not` 与 `in`/`not in`，中间操作数只求值一次，后续操作数按前序比较结果短路；前置 `not` 按 Python precedence 包裹完整 comparison。equality 对布尔/数值使用数值等价，对同类 list/tuple 递归逐元素比较，list 与 tuple 跨种类恒不相等，异类标量安全返回 false。membership 当前支持 substring 以及 list/tuple 逐元素 equality。identity 当前支持 `None`/布尔 singleton；JavaScript 还保持 list/tuple 对象引用 identity，`cpp` 因值容器无法保持对象身份而以 `MPF2044` 失败关闭。数值/string 对象 identity 受实现驻留影响，以 `MPF2045` 拒绝。条件表达式按 Python 语法右结合，只执行被选中的分支。dict/set、自定义 iterable/`__contains__`、用户对象 equality 与对象生命周期仍未进入当前子集。

Python/Matlab/Fortran 都先把物理源码归一化为带首行位置的 logical statements，随后进入各自 statement lexer 和递归下降 parser；表达式跨度仍交给共享 Pratt parser。Fortran procedure 关键字保持上下文化，declaration attribute 支持 `INTENT(IN/OUT/INOUT)` 与 `OPTIONAL`；已知 interface 的 keyword actual 在 Analyzer 中规范化为 formal 顺序，缺省 optional 以中立 omitted-argument 节点表示。当前静态/assumed-shape rank 范围内，optional 标量/数组支持全部三种 intent，存在的可写 name/element 使用 mutable borrow，section 使用显式 copy-out/copy-in-out，optional-to-optional 使用 forwarding contract；这些决定驻留于 MIR call argument 和双目标 LIR。对同一已知根 storage 的多个 writable actual，连续区间、交错步长和 N 维矩形 block 只有在规范化 region 被证明两两不相交时放行，重叠或动态边界以 `MPF2038` 拒绝。assumed-rank、动态 extent 组合、pointer/target association、generic interface、module procedure 与嵌套 procedure 尚未支持。

公共 `TranspileOptions::language_version` 和 CLI `--language-version` 选择 frontend manifest 声明范围内的标准；`{0,0}`/`latest` 选择该 frontend 上限。Python 使用 `major.minor`，Fortran 使用标准年份，Matlab 既接受 `R2024b` 也接受 `2024.2`。超出范围或在旧标准中使用较新 feature 以 `MPF1201` 失败关闭；当前细粒度 gate 已覆盖 Python 3.8 positional-only parameter 和 Fortran 2003 bracket array constructor。版本 contract 已完成不等于官方 grammar 已完整覆盖。

Fortran `SELECT CASE` selector 只求值一次。CASE bound 当前接受 integer、character、logical literal 以及带一元正负号的数值 literal；integer/character 支持单值、`lower:upper`、`:upper`、`lower:`，logical 只支持单值。Analyzer 在生成前拒绝动态 bound、类型不匹配、反向或重叠范围和重复 default，并把全部 CASE 分支纳入确定赋值合流。character 比较按 Fortran 规则在右侧补空格；named construct、named `EXIT`、derived-type selector、`SELECT TYPE` 与 `SELECT RANK` 尚未支持。

## 目标后端

两个目标后端是独立构建和安装组件。通用前端与语义分析不依赖任何目标；每个后端在 emitter 前运行自己的 capability validator。默认构建同时包含两者，也可通过 CMake 选项生成 javascript-only、cpp-only 或 core-only 包。`cpp` 是目标身份，当前输出标准为 C++17。

| 能力 | JavaScript | C++（当前 C++17） |
|---|---|---|
| 输出形态 | ESM 或 strict script | 函数模板及可执行 translation unit |
| 表达式优先级 | AST 驱动，必要时加括号 | AST 驱动，必要时加括号 |
| 幂、floor division | `**`、`Math.floor` | `std::pow`、`std::floor` lowering |
| 数学 intrinsic | `Math.*` | `<cmath>` 的 `std::*` |
| 多值/tuple | 多输出调用返回 Array；解构赋值只求值一次 | 多输出调用返回 `std::tuple`；临时 tuple 与 `std::get<N>` 只求值一次 |
| Python 比较对象模型 | 私有 `Symbol` 标记 tuple；递归 equality、reference identity、string/list/tuple membership | 递归跨元素类型 equality/membership、singleton identity；sequence identity 失败关闭；comparison temporary 固定左到右求值 |
| 函数依赖与递归 | 函数声明提升；支持当前函数子集的前向、直接和互递归 | callee-first 定义；静态返回类型递归使用模板声明，未知递归返回以 `MPF2035` 拒绝 |
| list/矩形数组 | 嵌套 JavaScript Array | element-type/shape 驱动的递归 `std::vector<T>` |
| 安全索引 | runtime base/负下标/逐维 bounds/列主序检查 | `index` 与二维 `matrix_linear_index` runtime |
| shape 聚合/变形 | 递归 `sum`/`numel`、二维 `length`/`reshape` | 模板递归聚合与 typed column-major reshape |
| section runtime | selector 递归读取/原位 mutation、Python resize、inclusive colon、列主序 flatten/mutation | typed `slice`/`column`/`columns`、`assign_*` 与列主序 flatten/mutation |
| 输出 runtime | `console.log` | 内嵌泛型 `mpf_runtime::print` |
| 自动验证 | Node.js syntax + execution | 平台 C++17 编译器 compile + execution |
| 名称安全 | JS 保留字确定性改写 | C++ 保留字改写并隔离在 namespace |

C++17 后端使用函数模板保持基础参数类型，并根据语义分析结果生成标量和任意 rank 递归 `std::vector` 声明；Python source call 在 Analyzer 后已成为完整位置实参序列。普通 Fortran IN 参数的 `const T&`、OUT/INOUT 的 `T&`，以及 optional formal 的具体 `mpf_runtime::optional_argument<T>` 已作为 `cpp` LIR v12 参数 ABI/访问计划固化。JavaScript 的 script/ESM、explicit export、value/reference-box ABI、一般 N 维默认数组初始化与作用域声明顺序同样驻留于 JavaScript LIR v12；两者都以 `SymbolId` inventory 和 statement/body/alternative `ScopePlan` 固化 lexical binding/declaration。module/translation-unit layout、强类型 comparison、custom call、first-result、N-D section、selector/range/for/loop-else/return 均由目标 plan 固化。逐实参 plan 同时保存 optional-forward/box/copy ownership 与写回形式，JavaScript writable call 选择 arrow IIFE，C++ section copy-in/out 与 comparison evaluation 选择 reference lambda，调用或操作数只求值一次并按计划保存。optional runtime 同时保存 absent、外部引用或临时 owned value；section actual 由 JavaScript selector-aware N 维 runtime 或 C++17 typed copy-out 回写。共享 Analyzer/MIR/alias 层在进入目标前完成静态 N 维 region 证明，两个后端不重复解释 selector。Analyzer 以 `MPF2038`—`MPF2041` 拒绝不可定义、未决/重叠 alias、shape/type 或 association 不匹配，并以 `MPF2044`/`MPF2045` 约束 comparison 可保持边界。广播、动态 rank、跨一般 pointer/view 的区域证明、矩阵乘法以及完整源语言对象模型尚未支持。

当前 declarative corpus 在同一 differential case 中直接比较 CPython 3.14 或 gfortran 严格 `-std=f2018` reference mode、Node.js、生成 C++17 与 oracle；`MPF_FORTRAN_REFERENCE_STANDARD` 允许工具链支持后切换到 `f2023`。这个外部编译器模式只描述当前 corpus 的 reference 执行环境，不降低 MPF frontend 的 Fortran 2023 版本化目标。Matlab case 当前直接比较 Node.js、生成 C++17 与 oracle；源程序执行门禁将在 CI 提供可授权的 Matlab runner 或明确选定 Octave 兼容策略后加入。
