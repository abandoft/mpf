# 语言支持矩阵

本表描述已由自动测试保证的当前语言子集，不是目标版本的完整兼容声明。

TypeScript 6 已纳入产品目标，但当前尚未注册 TypeScript frontend descriptor，也没有可声明支持的 TypeScript 语法子集；其接入计划见路线图 M5。

本表只描述语言行为覆盖，不以内部架构工作代替语言兼容声明。生产路径已经经过语言专属 PMR arena AST→HIR→revision-checked Analyzer side table→带 CFG/storage/alias 的 MIR→目标 semantic/rendered LIR→纯 emitter，并有 source map、fuzz/resource 和性能门禁；TypeScript 等新前端仍必须遵守独立 AST→HIR contract。静态一般 rank 的声明、RESHAPE 和直接 section 已完成，但尚未覆盖的官方 grammar、动态 rank/广播、精确 N 维 storage overlap/alias 与跨函数语义仍见 [商业级编译器管线方案](COMPILER_PIPELINE.md) 和 [TODO](../TODO.md)。

表中的“支持”表示当前子集已经进入语义/后端测试；可执行能力还应进入差分 corpus。只在某个目标可表示的结构必须由另一个目标的 capability validator 明确拒绝，不能静默改变语义。

| 能力 | Python | Matlab | Fortran |
|---|---|---|---|
| 源码形态 | 缩进式 logical statements；括号隐式续行、`\` 显式续行、行内注释、tab 展开、顶层分号；statement token/span stream | script logical statements；`...`、多行矩阵、行/block comment、顶层分号/逗号；statement token/span stream | free/fixed form 自动或显式选择；`&`/第 6 列 continuation、注释、分号 logical statements；statement token/span stream |
| 标量数字、字符串、布尔值 | 基础支持 | 基础支持 | 基础支持 |
| 标量算术与比较 | 支持；relational/equality 比较链短路且各操作数最多求值一次；已覆盖 bool/number 与当前同类递归 list equality | 支持，`^` 转为 `**` | 支持，含点式比较/逻辑运算符 |
| truthiness 与逻辑 | 数字、字符串、list、`None`；`not`；operand-returning、短路且单次求值的 `and/or` | 标量逻辑基础支持 | logical `.not./.and./.or.` 基础支持 |
| 标量转换 | `float` 的数字/布尔/字符串基础转换，含 NaN/Infinity | 尚未系统建模 | 声明赋值的基础转换 |
| 标量赋值 | 支持；固定 tuple/list 解包支持递归圆括号/方括号 pattern，每层一个 starred target | 支持 | 支持，声明初始化亦支持 |
| 输出 | `print(...)` | `disp(...)`/`display(...)` | `print *,`、`write(*,*)` |
| 条件/选择 | `if`/`elif`/`else`；右结合、惰性条件表达式 | `if`/`elseif`/`else`/`end` | 块 `IF`/`ELSE IF`/`ELSE`/`END IF`；integer/character/logical `SELECT CASE`，含单值、范围与 default |
| 函数 | 基础 `def`、参数、`return`；不可变标量默认值、keyword actual、`/` positional-only、裸 `*` keyword-only；前向调用；静态可表示返回的直接/互递归 | 基础文件级 local `function`；前向调用；单/多输出及跨函数转发；`[a,b]=f(...)` 单次调用；标量上下文选择首输出 | 基础 internal/external `FUNCTION`/`SUBROUTINE`、类型前缀、`RESULT`、`CALL`、`RETURN`、`RECURSIVE` 与 `INTENT(IN/OUT/INOUT)`；默认 intent；标量、整数组、数组元素及一/二维 section actual 写回；基础 keyword association；标量及一/二维数组 `OPTIONAL` 的 IN/OUT/INOUT、`PRESENT`、缺省调用与 optional 透传；前向/递归调用 |
| 常用数学 intrinsic | 映射到目标数学库 | 映射到目标数学库 | 映射到目标数学库 |
| 容器字面量 | 任意深度矩形嵌套 list | 逗号/空格列、分号分行矩阵；`reshape` 接受任意已知 rank 源和非空维度列表 | `[...]`、旧式 `(/.../)`；任意已知 rank 源/目标 `RESHAPE` |
| 标量索引读取/写入 | 0-based，支持负下标与任意深度链式索引 | 1-based 行列索引；二维线性索引为列主序 | 1-based；引用 rank 必须匹配声明 rank |
| section/slice 读取 | `start:stop:step`，缺省 bound、负 step、exclusive stop/clamp；直接多 selector 支持任意静态 rank | `:`, `start:stop`, `start:step:stop`；行/列/block/线性选取及任意静态 rank 直接 selector | `lower:upper:stride`；任意静态 rank 直接 section、缺省 bound、负 stride |
| section/slice 写入 | 一维 list 普通切片可变长度替换；extended slice 等长替换；固定 shape 多 selector 写入 | 行、列、block、列主序线性 colon 及任意静态 rank 直接 section 赋值；支持标量扩展 | 任意静态 rank 直接 array section 赋值；支持标量扩展 |
| 长度与求和 | `len`、一维/行 `sum` | `length`/`numel`；`sum` 当前限一维 | `SIZE`、`SUM` 全元素归约 |
| shape | 任意深度矩形字面量、任意 rank 静态/动态 slice extent | 矩阵、任意 rank `RESHAPE` 及直接 section 的静态/动态 extent | 任意 rank 常量/assumed-shape 声明、`RESHAPE` 与直接 section shape |
| while | 基础支持 | 基础支持 | `DO WHILE` 基础支持 |
| 计数循环 | `for ... in range(...)` | `for i=start:step:stop` | counted `DO` |
| 循环变量结束语义 | 保留最后一次迭代值 | 保留最后一次迭代值 | 保留终止增量后的值 |
| 循环控制 | `break`/`continue` | `break`/`continue` | `EXIT`/`CYCLE` |
| 正常结束分支 | `for/while ... else` | 不适用 | 不适用 |
| 推导式/并行循环 | 尚未支持 | `parfor` 尚未支持 | `DO CONCURRENT` 尚未支持 |
| 类、模块、包 | 尚未支持 | 尚未支持 | 尚未支持 |
| 异步、并行、协程 | 尚未支持 | 尚未支持 | 尚未支持 |

## 版本目标

- Python：以 3.14 grammar 和语言参考为上限，并为旧版本保留语法模式。
- Matlab：以 R2024a/R2024b 语法和核心语言语义为目标；工具箱 API 单独建模。
- Fortran：以 2023 标准为上限，同时覆盖常见 fixed/free form 历史源码。
- TypeScript：以 6.x grammar、类型擦除和 ECMAScript 运行时语义为目标；当前尚未实现。

Fortran source form 默认由扩展名决定：`.f`、`.for`、`.ftn`、`.f77` 使用 fixed form，现代扩展名使用 free form；API 的 `FortranSourceForm` 或 CLI `--fortran-form` 可覆盖。fixed form 遵循列 1–5 label、列 6 continuation、列 7–72 statement field；当前明确拒绝 tab-form。预处理与 `INCLUDE` 尚未纳入此层。

完整覆盖将按“lexer/parser → 名称绑定 → 类型/shape 推导 → 语义 lowering → runtime”推进。每个矩阵条目只有在语义测试和差分执行测试落地后才能标记为支持。

Python parameter parser 记录 positional-only、positional-or-keyword 与 keyword-only 种类以及逐参数 default AST。Analyzer 依据已知 user-function signature 将 positional/keyword actual 规范化为 formal 顺序、补入默认值，并以 `MPF2034`/`MPF2041` 拒绝重复、未知、缺失或种类不匹配。当前默认值限定为无副作用的不可变标量 literal（含一元正负号），从而保证插入调用时不会破坏 Python 定义时单次求值语义；可变默认值、调用表达式、annotation、`*args` 与 `**kwargs` 尚未支持。

Python assignment parser 将裸/圆括号/方括号 target 解析为独立递归 `AssignmentPattern`，支持 nested sequence、单目标尾随逗号以及每层最多一个 starred name。Analyzer 接受固定 tuple/list literal、带可靠静态 extent 的 list 名称，以及逐层传播元数据的已知 user-function sequence return；它递归计算每个叶子的 type/element-type/shape 与访问路径，并以 `MPF2042` 拒绝数量不匹配、多个同层 star、非 sequence 嵌套值或动态未知长度。重复名称按 Python 从左到右覆盖；star capture 始终形成 list，C++ 对异质 capture 以 `MPF2020` 失败关闭，JavaScript 保持动态可用。字符串、一般 iterable、运行时未知长度与属性/下标 target 尚未进入当前解包子集。

Python comparison chain 当前覆盖 `<`、`<=`、`>`、`>=`、`==`、`!=`，中间操作数只求值一次，后续操作数按前序比较结果短路。条件表达式按 Python 语法右结合，只执行被选中的分支。`is`/`is not`、`in`/`not in` 尚未进入当前表达式子集；JavaScript 表示也尚未完整区分 list 与 tuple 的对象种类，因此跨 sequence kind equality/identity 不作支持承诺。

Python/Matlab/Fortran 都先把物理源码归一化为带首行位置的 logical statements，随后进入各自 statement lexer 和递归下降 parser；表达式跨度仍交给共享 Pratt parser。Fortran procedure 关键字保持上下文化，declaration attribute 支持 `INTENT(IN/OUT/INOUT)` 与 `OPTIONAL`；已知 interface 的 keyword actual 在 Analyzer 中规范化为 formal 顺序，缺省 optional 以中立 omitted-argument 节点表示。当前静态/assumed-shape rank 范围内，optional 标量/数组支持全部三种 intent，存在的可写 element/section actual 延续引用或 copy-in/copy-out。assumed-rank、同一根 storage 的多写回参数、generic interface、module procedure 与嵌套 procedure 尚未支持。

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
| 函数依赖与递归 | 函数声明提升；支持当前函数子集的前向、直接和互递归 | callee-first 定义；静态返回类型递归使用模板声明，未知递归返回以 `MPF2035` 拒绝 |
| list/矩形数组 | 嵌套 JavaScript Array | element-type/shape 驱动的递归 `std::vector<T>` |
| 安全索引 | runtime base/负下标/逐维 bounds/列主序检查 | `index` 与二维 `matrix_linear_index` runtime |
| shape 聚合/变形 | 递归 `sum`/`numel`、二维 `length`/`reshape` | 模板递归聚合与 typed column-major reshape |
| section runtime | selector 递归读取/原位 mutation、Python resize、inclusive colon、列主序 flatten/mutation | typed `slice`/`column`/`columns`、`assign_*` 与列主序 flatten/mutation |
| 输出 runtime | `console.log` | 内嵌泛型 `mpf_runtime::print` |
| 自动验证 | Node.js syntax + execution | 平台 C++17 编译器 compile + execution |
| 名称安全 | JS 保留字确定性改写 | C++ 保留字改写并隔离在 namespace |

C++17 后端使用函数模板保持基础参数类型，并根据语义分析结果生成标量和任意 rank 递归 `std::vector` 声明；Python source call 在 Analyzer 后已成为完整位置实参序列。JavaScript 同时保留可读的 immutable scalar default 签名，并使用同一规范化调用。普通 Fortran IN 参数生成 `const T&`，OUT/INOUT 生成 `T&`；optional formal 生成具体标量或递归 vector 类型的 `mpf_runtime::optional_argument<T>`，同时保存 absent、外部引用或临时 owned value。JavaScript 以 `undefined` 表示缺省 optional，存在的 writable actual 使用 `{value}` box，并在 optional-to-optional 调用中直接透传 box/undefined。section actual 由 JavaScript selector-aware N 维 runtime 或 C++17 模板递归/typed copy-out 回写。Analyzer 以 `MPF2038`—`MPF2041` 拒绝不可定义、alias、shape/type 或 association 不匹配。广播、动态 rank、精确 N 维 overlap/多写回 alias、矩阵乘法以及源语言特有对象模型尚未支持。

当前 declarative corpus 在同一 differential case 中直接比较 CPython 3.14 或 `gfortran -std=f2023`、Node.js、生成 C++17 与 oracle。Matlab case 当前直接比较 Node.js、生成 C++17 与 oracle；源程序执行门禁将在 CI 提供可授权的 Matlab runner 或明确选定 Octave 兼容策略后加入。
