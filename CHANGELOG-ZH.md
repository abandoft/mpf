## 0.4.9

- Matlab 矩阵左除现可求解静态满秩稠密实数方阵、超定和欠定系统，并支持一列或多列右端项。
- Matlab 矩阵右除现支持 row vector 或 rank-2 左操作数，以及静态满秩稠密实数方阵或矩形除数。
- Matlab 矩阵幂现支持静态方阵稠密实数矩阵的零次、正整数次和负整数次幂，指数限制在 ECMAScript safe-integer 范围内。
- 方阵求解使用部分主元选取；矩形最小二乘和最小范数求解在生成的 JavaScript 与 C++17 中均使用带列主元的 Householder QR。
- 数组除以 scalar 和 scalar 左除数组现可保持 Matlab 矩阵运算符语义，无需改写为逐元素运算符。
- Matlab 索引新增保序 numeric selector array、重复下标、空 selector，以及线性或逐维位置的 logical selector。
- shape 只能在 runtime 确定的 logical selector 现在由生成代码验证，不再要求所有 mask 大小都能在编译期求出。
- 矩阵运算种类、求解类别及输入/输出 shape 现在以显式、可验证事实贯穿 HIR、MIR、JavaScript LIR 和 `cpp` LIR。
- Semantic、MIR 与目标 LIR schema 分别升级到 v4、v9 和 v15；每个 scalar、slice、numeric、logical 或 empty selector 都拥有逐下标可验证身份。
- 秩亏系统、非方阵幂、矩阵指数、非有限求解值，以及不安全或非整数指数会确定性失败关闭。
- 新增可执行的方阵和矩形求解示例、双目标差分验证，以及秩亏系统的生成 runtime 拒绝门禁。
- 扩充 Matlab 矩阵求解/幂和通用索引的 fuzz、source map、生成代码、runtime 拒绝及编译性能覆盖。

## 0.4.8

- Matlab 数组算术现支持静态已知 N 维 shape 的 compatible-size 隐式扩展，包括 singleton 维和缺失的尾随维度。
- Matlab 关系运算现可生成布尔数组，并支持标量、完全相同 shape 和 compatible-size 扩展，可直接表达 `A(A >= 30)`。
- JavaScript 数组扩展采用一次展平和预计算 stride 的内核；标量及完全相同 shape 的运算继续使用直接快速路径。
- 生成的 C++17 增加目标独立的类型化 N 维扩展和数组比较实现，与 JavaScript 输出保持一致。
- Matlab 共轭转置 `'` 与非共轭转置 `.'` 现在独立解析，并支持当前实数 vector 和 rank-2 数组。
- Matlab `end` 可在静态已知 extent 时按当前索引维度或线性元素总数解析，并可参与算术和 colon 表达式。
- Matlab 逻辑 mask 现支持列主序线性读取，以及标量或 vector 写入，并严格检查 mask 大小和 replacement shape。
- 动态 extent、高 rank 转置、不兼容 mask、通过 `end` 扩容、矩阵除法和矩阵幂会在生成代码前使用专用诊断失败关闭。
- 新增五个可执行 Matlab 示例和差分 case，双目标覆盖隐式扩展、转置、`end`、逻辑索引和广播比较。
- 扩充 Matlab fuzz 回归输入，并加强 broadcast plan、转置身份、逻辑选择和优化后中间表示的跨层验证。

## 0.4.7

- Matlab 现在区分矩阵运算符与 `.*`、`./`、`.\`、`.^` 等逐元素运算符，不再在解析阶段丢失语义。
- 新增二维 numeric 矩阵乘法，并同时生成可执行的 JavaScript 与 C++17 实现。
- 新增同 shape 数组及 scalar expansion 的 `+`、`-`、`.*`、`./`、`.\`、`.^` 基础运算。
- shape 不相容或尚未支持的矩阵除法、矩阵幂会给出 `MPF2046`，不会生成不可靠的目标代码。
- Matlab 运算符身份使用强类型事实贯穿 AST、HIR、MIR 与目标 LIR，便于后续扩展广播、转置和数值类型。
- JavaScript 与 C++ 后端源码改为独立语言目录，文件名移除重复目标前缀，新增目标时边界更清晰。
- 新增 Matlab 数组运算差分示例、正负语义测试、目标代码检查和 fuzz 回归样本。
- Matlab → JavaScript 语言支持矩阵、诊断文档和项目路线图已同步当前能力与已知限制。
- C++ 输出现在为词法作用域中同名的 symbol 分配不同目标标识符，在保持源语言作用域语义的同时避免 MSVC 警告升级为错误。

## 0.4.6

- CMake 安装包改为精确版本匹配；使用方需要通过 `find_package(mpf 0.4.6 EXACT ...)` 查找当前版本。
- 源语言名称统一为 `matlab`、`python`、`fortran`、`typescript`，输出目标统一为 `javascript`、`cpp`。
- 移除 `py`、`f90`、`ts`、`js`、`c++` 等名称缩写；文件扩展名自动识别不受影响。
- C++ API 的语言名称和版本解析接口现在使用 `std::optional` 表示无效输入，不再静默选择默认语言或目标。
- 前端和后端扩展接口升级至 API v6，并移除旧名称别名字段。
- CMake 包只公开标准 component 状态和导出目标，移除早期开发快照中的大写后端状态变量。
- 诊断文本和 JSON 现在始终提供完整源码范围，便于编辑器准确标记问题位置。
- 安装示例、嵌入示例、扩展指南和版本文档已同步到当前接口。

## 0.4.5

- 改进分支、循环和函数调用中的内存依赖分析，使数组及 section 写入顺序更加可靠。
- 能够区分不相交的 N 维区域，减少对安全访问的保守限制。
- 编译报告新增内存依赖统计，便于定位复杂控制流和潜在性能问题。
- 优化大型控制流图和依赖去重，相关基准场景耗时由约 354 ms 降至约 43 ms。
- 加强循环携带依赖、未知外部调用和索引写入的回归与 fuzz 验证。

## 0.4.4

- 更精确地跟踪变量、数组元素、section、参数复制和写回产生的读写行为。
- 跨函数调用传播参数的实际访问区域，改善可写参数和数组 section 的正确性检查。
- 改进别名分析，可识别明确不相交、明确相同和无法确定的内存区域。
- 修复优化后内存访问信息可能与生成代码不同步的问题。
- 扩充索引、切片、写回和跨函数数组访问的稳定性测试。

## 0.4.3

- 新增统一的 N 维数组区域模型，支持连续区间、步长 section、负步长和多维矩形区域。
- Fortran 现在允许把同一数组中可证明互不重叠的多个 section 作为可写实参传入过程。
- 对真实重叠或边界无法确定的可写实参继续给出安全诊断。
- 改进空 section、大步长区域和列主序线性选择的处理。
- 新增 Fortran 不相交区域示例及跨 gfortran、JavaScript、C++ 的结果验证。

## 0.4.2

- 在 JavaScript 和 C++ 后端之前加入共享优化流程，两个目标使用同一份优化结果。
- 新增安全的整数与布尔常量折叠，并避免超出 JavaScript 精确整数范围时改变程序语义。
- 规范化静态数组 shape，清理无效指令和简单冗余控制流。
- 改进块参数复制传播，同时保留一般分支合流和动态值语义。
- 编译报告新增优化前后规模、常量折叠和清理统计。
- 新增 Python 优化示例，并验证源程序、JavaScript 和 C++ 输出结果一致。

## 0.4.1

- TypeScript `let` 和 `const` 支持嵌套块作用域、同名遮蔽及对外层变量赋值。
- TypeScript 新增标准 C 风格 `for` 循环，支持初始化、条件、更新、`break` 和 `continue`。
- 修复生成 JavaScript/C++ 时局部变量作用域和生命周期不准确的问题。
- TypeScript `number` 按 ECMAScript 数值语义处理，数组索引拒绝无法安全表示的非整数值。
- JavaScript 输出只携带当前程序实际需要的运行时辅助代码。
- 新增 TypeScript 块作用域和 `for` 循环的多目标执行验证。

## 0.4.0

- 新增 TypeScript 前端，可通过 `.ts`、`.mts` 和 `.cts` 文件自动识别。
- 首批支持 `let`/`const`、标量与类型化数组、赋值、函数、默认参数、条件、`while`、循环控制和 `console.log`。
- 支持擦除当前子集中的类型标注，并生成 JavaScript 或 C++17。
- TypeScript `export function` 可生成对应的 JavaScript ESM 导出。
- 对 `var`、宽松相等、箭头函数、模板字符串和尚未建模的对象语义给出明确诊断，避免生成行为不一致的代码。
- 新增 Node.js 源码、生成 JavaScript、生成 C++ 和声明结果的四路验证。

## 0.3.9

- 改进条件表达式、Python `and`/`or` 和比较链的短路求值，确保操作数只在需要时执行。
- 变量读取、初始化、赋值、索引写入和参数写回采用统一的中间表示，减少两个后端之间的行为差异。
- 修复可写 section 参数在 copy-in、copy-out 和 writeback 阶段的副作用跟踪。
- 加强函数参数、返回 shape、动态 extent 和惰性分支的验证。
- 改善大型程序的中间表示查询和验证性能。

## 0.3.8

- 重构编译器中间表示，降低大型表达式树和控制流的遍历成本。
- JavaScript 与 C++ 后端改为从统一的值、类型、shape 和存储信息生成代码。
- 改进损坏或不完整中间状态的检测，避免错误继续传播到代码生成阶段。
- 本版本不扩大语言语法范围，重点提升编译稳定性和后端一致性。

## 0.3.7

- Python、Matlab 和 Fortran 解析器改为直接构建各自的语言 AST，减少重复复制和峰值内存。
- 改进三种语言的错误恢复，解析失败后不会发布不完整语法节点。
- 不同语言的 AST 现在相互独立，为后续扩展完整官方 grammar 提供清晰边界。
- 移除不再使用的共享语法入口，降低新语言接入时的耦合。

## 0.3.6

- Python 新增 `is`、`is not`、`in` 和 `not in`。
- 改进布尔、数值、字符串、list 和 tuple 的相等比较语义。
- 支持字符串、list 和 tuple membership，以及混合 comparison chain。
- JavaScript 输出能够区分 list 与 tuple，并保持支持范围内的引用身份。
- C++ 输出新增递归容器比较和 membership 支持；无法可靠表达的 identity 比较会明确拒绝。
- 新增 Python 比较语义的 CPython、JavaScript 和 C++ 结果验证。

## 0.3.5

- 改进跨函数类型、shape、tuple 返回值和引用参数的检查。
- 更准确地处理只读借用、可写借用、copy-out、copy-in/out、optional 转发和省略参数。
- 修复函数调用中的数组 section 写回、可选参数转发和潜在别名问题。
- JavaScript 与 C++ 后端分别规划函数 ABI、临时值、声明顺序和模块布局，生成任一目标不再依赖另一目标。
- 改善大型函数图和复杂调用场景的分析性能与错误诊断。
- 加强资源上限检查，避免参数展开后绕过节点数量限制。

## 0.3.4

- 静态数组对象语义扩展到一般 N 维声明、嵌套 shape、`RESHAPE`、索引和 section 读写。
- 新增三维 Fortran tensor 示例，并验证 gfortran、JavaScript 和 C++ 输出一致。
- 公共 API 和 CLI 新增源语言版本选择；不支持的版本特性会给出明确诊断。
- 新增源码映射输出、依赖清单、资源限制和机器可读编译报告。
- 前端、共享分析和目标后端改为清晰分层的编译管线，JavaScript 与 C++ 后端保持独立。
- GitHub 自动化按验证、平台、质量、Sanitizer、覆盖率、性能、安全和发布职责拆分。
- 增加 fuzz、性能基线、安装后消费和外部扩展示例。

## 0.3.3

- 前端和后端改为可查询的 descriptor/registry 架构，便于增加新的源语言和输出目标。
- 公共 API 可查询已构建的源语言和目标后端。
- 内置函数绑定由各语言和目标显式声明，缺少目标实现时会在生成代码前报错。
- 禁用某个后端时仍可查询其元数据，且不会编译或链接该后端实现。
- 新增前端、后端和内置函数绑定的扩展指南。

## 0.3.2

- Python 新增链式比较和右结合条件表达式。
- 比较链保持从左到右、单次求值和短路行为。
- 条件表达式在 JavaScript 与 C++ 输出中保持惰性分支和 Python truthiness。
- 对 C++ 无法静态表达的比较或分支结果给出明确诊断。
- 新增 CPython、JavaScript 和 C++ 四路结果验证。

## 0.3.1

- Fortran 新增 `SELECT CASE`、`CASE DEFAULT` 和 `END SELECT`。
- 支持 integer、character 和 logical selector，以及单值、闭区间和省略边界的区间。
- 新增区间类型、反向区间和重叠 CASE 检查。
- character CASE 按 Fortran 空格补齐规则比较。
- 改进多分支后的确定赋值和终止流分析。

## 0.3.0

- 新增统一的 C++ 代码格式化和静态检查配置。
- 新增源码覆盖率报告，并设置 85% 的生产代码行覆盖率门槛。
- GitHub 自动化加入格式、静态检查、覆盖率、CodeQL 和依赖安全检查。
- 修复质量检查发现的重复分支和无效状态更新。
- 所有质量报告继续统一生成到 `build/` 目录。

## 0.2.9

- Python 解包赋值支持嵌套 tuple/list pattern 和 starred target。
- 支持 star 位于任意位置、空 capture、嵌套 capture 和重复名称覆盖。
- 解包右侧只求值一次，并在 JavaScript 与 C++ 输出中保持赋值顺序。
- 对动态长度、结构不匹配和 C++ 无法表示的异质 star 结果给出明确诊断。
- 新增 CPython、JavaScript 和 C++ 四路解包验证。

## 0.2.8

- C++ 输出目标的统一名称改为 `cpp`。
- 公共 API 使用 `TargetLanguage::cpp`，CLI 使用 `--target cpp`。
- CMake 后端开关、组件名和导出目标同步使用 `cpp`。
- 生成代码标准仍为 C++17；标准版本不再出现在目标身份中。
- 这是 0.x 阶段的命名变更，旧名称不再保留。

## 0.2.7

- Python 新增平坦 tuple/list 解包赋值。
- 支持裸 `a, b`、圆括号、方括号和单目标尾随逗号。
- 支持 tuple-return 函数、交换赋值、异质 tuple 和重复目标。
- JavaScript 与 C++ 输出均保证右侧表达式只求值一次。
- 动态未知长度或目标数量不匹配时会给出明确诊断。

## 0.2.6

- Python 函数支持标量默认参数。
- 新增 positional-only `/`、keyword-only `*`、关键字实参和尾随逗号。
- 调用分析能够补齐默认值，并检查重复、未知、缺失及位置错误的实参。
- 当前默认值限于无副作用的不可变标量；容器、调用和标识符默认值会明确拒绝。
- JavaScript 与 C++ 输出使用相同的参数关联结果。

## 0.2.5

- Fortran optional 参数扩展到当前支持范围内的标量和数组 `INTENT(IN/OUT/INOUT)`。
- 支持 optional 可写实参、数组元素、整数组和一/二维 section 的有序写回。
- optional 参数跨过程转发时能够保持缺省状态或引用身份。
- 修复缺省 optional OUT 参数被错误视为未初始化读取的问题。
- JavaScript 与 C++ 输出新增对应的 optional reference 运行时支持。

## 0.2.4

- Fortran 新增关键字实参和 `OPTIONAL` 声明属性。
- 支持标量 `OPTIONAL, INTENT(IN)`、`PRESENT` 和 optional 参数转发。
- 调用时检查未知关键字、重复关联、缺少必需参数及 positional-after-keyword。
- JavaScript 使用 `undefined` 表示缺省实参，C++ 使用类型化 optional 值。
- 尚未支持的 optional 数组和可写 optional 参数会明确拒绝。

## 0.2.3

- Fortran 可写过程实参扩展到数组元素和一/二维 section。
- 连续与非连续 section 通过 copy-in/copy-out 保持写回语义。
- Fortran function 的返回值与 section 写回按正确顺序执行。
- 对可能重叠的多个可写实参采用保守诊断，避免生成不安全代码。
- 新增 gfortran、JavaScript 和 C++ 的 section 实参验证。

## 0.2.2

- Fortran 支持一维和二维 assumed-shape dummy array。
- 调用时检查标量/数组分类、rank、静态 extent 和元素类型。
- C++ 输出使用引用共享数组存储，JavaScript 输出保持数组 OUT 参数的原位更新。
- 非 dummy assumed-shape 声明和尚不安全的非连续可写 section 会明确拒绝。

## 0.2.1

- Fortran 新增 `INTENT(OUT)` 和 `INTENT(INOUT)`。
- OUT/INOUT 实参按引用传递，并在调用后更新调用方变量。
- 调用分析允许未初始化 OUT，要求 INOUT 已赋值，并检查所有返回路径。
- 拒绝把同一变量同时绑定到多个潜在写回参数。
- JavaScript 与 C++ 后端分别使用 reference box 和 C++ 引用保持语义。

## 0.2.0

- Fortran 新增 function、subroutine、`RESULT`、`RETURN`、`RECURSIVE` 和 `CALL`。
- 支持内部/外部过程、dummy parameter、提前返回和具名 `END` 校验。
- 支持递归标量 function 和递归 subroutine。
- 区分 function expression 与 `CALL` subroutine 的使用位置，并对混用给出诊断。
- 在完整引用参数支持前，可能被错误按值传递的 dummy 修改会被安全拒绝。

## 0.1.9

- 改进函数依赖分析，支持前向调用、直接递归和互递归。
- 函数返回类型、元素类型和 shape 可沿调用链传播。
- C++ 输出会按依赖顺序生成函数，并为可静态描述的递归函数建立前置声明。
- 无法为 C++ 建立可靠返回类型的递归函数会明确拒绝，JavaScript 输出仍可独立使用。
- 改进 Matlab 多输出值跨函数转发。

## 0.1.8

- Matlab 新增 `[a, b] = f(...)` 多输出赋值。
- Matlab 多输出函数在单值上下文中按语言规则选择第一个输出。
- JavaScript 使用数组与解构，C++ 使用 `std::tuple` 与 `std::get`。
- 两个后端都保证多目标赋值的右侧只求值一次。
- 新增输出数量、重复目标和非法右侧检查。

## 0.1.7

- Fortran 前端改用 token 化 statement lexer 和递归下降 parser。
- 支持当前 program、声明、IF、DO、循环控制、PRINT/WRITE、CALL 和赋值子集。
- free/fixed source form 均使用新的解析流程。
- 修复合法实体名被错误识别为关键字的问题。
- 修复标准 `WRITE(*,*) value` 解析，并保留常见带逗号形式。

## 0.1.6

- Matlab 前端改用 token 化 statement lexer 和递归下降 parser。
- 支持当前函数、多输出、条件、循环、display、赋值、索引赋值和表达式语句子集。
- 正确区分字符向量、共轭转置和非共轭转置。
- 改进 Matlab 函数返回类型传播和 C++ 前向调用生成。
- 新增 parser 错误恢复和 statement-token 结果验证。

## 0.1.5

- Python 前端改用 token 化 statement lexer 和递归下降 parser。
- 支持当前函数、条件、`while`、`for ... else`、return、循环控制、赋值和 print 子集。
- 表达式继续由统一的优先级 parser 处理，避免 statement 与 expression 语法不一致。
- 新增非法链式赋值、参数形态和孤立 clause 的诊断与恢复。
- 新增 CPython、JavaScript 和 C++ 的 statement-token 验证。

## 0.1.4

- Python 支持括号内隐式续行、反斜杠续行、tab 缩进和一行多个 simple statement。
- Matlab 支持 `...` 续行、多行矩阵、block comment 和一行多个 statement。
- Python 与 Matlab 的注释和字符串现在能够安全跨越逻辑行处理。
- 新增未闭合 delimiter、字符串、注释和错误续行的诊断。
- 新增多行源码的 JavaScript/C++ 结果验证。

## 0.1.3

- Python 条件和循环支持数字、字符串、list、`None` 与 NaN truthiness。
- Python `and`/`or` 保留操作数返回值、短路顺序和左操作数单次求值。
- JavaScript 与 C++ 输出保持支持范围内的惰性求值。
- Python `float` 新增数字、布尔和字符串基础转换，以及 NaN/Infinity 解析。
- C++ 无法静态统一逻辑表达式结果时会在生成前给出诊断。

## 0.1.2

- 建立统一的差分测试语料清单。
- 同一示例可直接比较源语言、生成 JavaScript、生成 C++ 和声明结果。
- 生成 C++ 使用与顶层项目相同的编译器和生成器进行真实编译执行。
- 失败时保留生成源码、编译日志和各执行路径结果。
- 自动化环境固定 Python 和 Node.js 版本，避免运行时缺失导致测试被静默跳过。

## 0.1.1

- Fortran 新增独立的 free-form 和 fixed-form 源码规范化。
- free form 支持 `&` 续行、续行间注释、字符常量续行和分号 statement。
- fixed form 支持 label field、第 6 列 continuation、第 7–72 列 statement 和传统整行注释。
- 根据文件扩展名自动选择 source form，也可通过 API 或 `--fortran-form` 显式指定。
- 新增孤立续行、未完成续行、错误字符续行和不安全 fixed-form 布局诊断。

## 0.1.0

- 新增多文件源码管理和稳定的源码位置标识。
- 所有诊断现在包含文件名、起止位置和源码片段。
- 公共 API 新增诊断文本渲染和 JSON diagnostics v1 输出。
- CLI 新增 `--diagnostics-format text|json`。
- 统一 CLI 退出状态，区分编译、参数、输入和输出错误。
- 安装 diagnostics v1 JSON Schema，并补充工具集成文档。

## 0.0.9

- Python 支持普通切片的可变长度替换和 extended slice 等长赋值。
- Matlab 支持行、列、矩形 block 和列主序线性 section 赋值。
- Fortran 支持一/二维 array section 赋值、shape 检查和标量扩展。
- JavaScript 与 C++ 运行时新增对应的原位 section 更新。
- 对临时 section 写入、shape 不匹配和 C++ 无法保持的动态容器变化给出诊断。

## 0.0.8

- JavaScript 与 C++ 后端拆分为独立组件，可分别启用或禁用。
- 生成任一目标不再要求构建另一目标后端。
- 公共 API 新增后端可用性查询；请求未构建后端时返回明确诊断。
- CMake 安装包新增 `core`、`javascript` 和 `cpp` component。
- 增加 javascript-only、cpp-only 和 core-only 的构建、安装及外部消费验证。
- 修复 Python ragged list 的 rank 信息丢失问题。

## 0.0.7

- Python 新增正负步长切片和更深的矩形嵌套 list shape 推导。
- Matlab 新增整行、整列、二维 block、步长 colon 和 `A(:)`。
- Fortran 新增一/二维 array section、默认边界和正负 stride。
- JavaScript 与 C++ 运行时新增通用 section 读取支持。
- 改进空 section、动态 extent、逐维边界和固定 shape 赋值检查。

## 0.0.6

- Python 支持矩形嵌套 list 和二维索引读写。
- Matlab 支持矩阵字面量、二维索引和列主序线性索引。
- Fortran 支持二维常量 extent、rank 检查及一/二维 `RESHAPE`。
- JavaScript 与 C++ 运行时新增多下标、递归聚合和列主序 reshape。
- C++ 对 ragged list 给出安全诊断。

## 0.0.5

- Python 新增一维 list 索引读写、负下标、`len` 和 `sum`。
- Matlab 新增行向量、1-based 索引、`length`、`numel` 和 `sum`。
- Fortran 新增一维定长数组、数组构造器、1-based 索引、`SIZE` 和 `SUM`。
- 新增静态 extent、同质元素、常量越界和索引类型检查。
- JavaScript 与 C++ 运行时新增对应的边界和索引基准处理。

## 0.0.4

- 新增 Python `elif`、Matlab `elseif` 和 Fortran `ELSE IF`。
- 新增 Python/Matlab `break`、`continue` 与 Fortran `EXIT`、`CYCLE`。
- Python 新增 `for ... else` 和 `while ... else`，包括嵌套循环中的正确 `break` 行为。
- 改进循环与函数上下文检查、不可达代码警告和返回路径分析。
- C++ 输出会拒绝不兼容返回类型及值返回与隐式空返回混用。

## 0.0.3

- 新增名称绑定、内置名称遮蔽、未定义标识符和确定赋值分析。
- 新增整数、实数、布尔和字符串基础类型推导。
- 新增 Python `range`/`while`、Matlab colon `for`/`while`、Fortran `DO`/`DO WHILE`。
- 保持三种语言各自的循环结束变量语义，并支持负 step。
- JavaScript 与 C++ 输出新增保留字和名称冲突安全改写。
- C++ 生成代码隔离到 `mpf_generated` namespace。

## 0.0.2

- 公共 API 和 CLI 新增 JavaScript/C++ 双输出目标选择。
- 新增独立 C++17 后端、基础运行时和可执行入口生成。
- 新增 UTF-8 源码位置、CRLF 处理和公共 token 模型。
- Python、Matlab 和 Fortran 表达式改用结构化 lexer/parser。
- JavaScript 输出新增安全的运算符优先级、Python floor division 和 list/tuple 支持。
- 新增生成 JavaScript 的 Node.js 验证和生成 C++ 的真实编译执行测试。

## 0.0.1

- 建立 MPF 公共 API、命令行工具、三语言基础标量转译、JavaScript 后端、测试和构建基础。
