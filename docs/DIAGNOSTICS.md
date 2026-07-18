# 诊断与 CLI 契约

MPF 当前为库调用、命令行、IDE 和 CI 使用同一个诊断模型；本文描述 0.5.4 源码树的唯一契约。每条诊断在构造时即包含 code、severity、消息、源文件身份以及完整的 1-based UTF-8 code-point range；renderer 不接收缺失结束位置的旧结构，也不合成兼容范围。

## 文本输出

`mpfc` 默认在标准错误输出位置、级别、code、消息、原始源码行和插入符：

```text
example.py:1:1: error MPF2001: undefined identifier 'missing'
 1 | print(missing)
   | ^
```

库调用可使用 `mpf::render_diagnostic_text`，并通过 `DiagnosticRenderOptions` 控制源码片段和彩色输出。

## JSON v1

`mpfc --diagnostics-format json ...` 在标准错误输出恰好写入一个 JSON 文档。即使成功且没有诊断，也会输出空的 `diagnostics` 数组。编译警告与后续输出错误会合并到同一个文档，禁止拼接多个 JSON 根对象。

```json
{
  "schemaVersion": 1,
  "diagnostics": [
    {
      "severity": "error",
      "code": "MPF2001",
      "message": "undefined identifier 'missing'",
      "source": "example.py",
      "sourceId": 0,
      "range": {
        "start": { "line": 1, "column": 1 },
        "end": { "line": 1, "column": 2 }
      }
    }
  ]
}
```

规范文件为 [diagnostics-v1.schema.json](schemas/diagnostics-v1.schema.json)。`schemaVersion` 标识当前 JSON 结构，CLI、schema、测试和 consumer 必须同步更新；项目进入稳定产品阶段前不提供历史 schema reader、字段双写或诊断码迁移层。

## 退出状态

| 状态 | 含义 |
|---:|---|
| 0 | 转译成功，可能包含 warning/note |
| 1 | 源码解析、语义或目标能力错误 |
| 2 | 命令行参数错误 |
| 3 | 输入读取错误 |
| 4 | 生成结果写入错误 |

驱动错误使用 `MPFCLI` code 空间：`MPFCLI0001` 为参数错误，`MPFCLI0002` 为输入错误，`MPFCLI0003` 为输出错误。JSON 模式对驱动错误同样生效。

## Source map 与编译报告

库 API 成功时通过 `TranspileResult::source_map` 返回 source map v3，并在 `TranspileResult::dependencies` 返回目标 lowering 已规范化的依赖清单；`CompilationReport` 记录 source 大小、总耗时、峰值 arena、逐阶段节点/耗时和 `mir_optimization` 统计。后者公开 folded/retired expression、removed instruction/block、propagated block argument、canonicalized shape 以及 instruction/block before/after，不包含源码内容。`emit_source_map=false` 可关闭 map 构建。

CLI 使用 `--source-map <path>` 写出标准 JSON map；该路径必须是文件，不能为 `-`，从而避免与生成代码或 diagnostics JSON 混入同一 stdout。map I/O 失败使用既有 `MPFCLI0003`/退出状态 4。

## 编译诊断索引

| 范围/代码 | 阶段与含义 |
|---|---|
| `MPF0002` | 无法确定源语言 |
| `MPF0003` | 请求的目标后端没有编入当前安装/构建 |
| `MPF0004` | 目标后端没有为已解析的 intrinsic 提供代码绑定；生成前失败关闭 |
| `MPF0005` | frontend AST/HIR contract、节点身份或 pass descriptor verifier 失败 |
| `MPF0006` | MIR 稠密表、CFG、所有权、dominance、type/shape/storage/effect verifier 失败 |
| `MPF0007` | 目标 legalization 或目标 semantic lowering plan 失败 |
| `MPF0008` | 目标 LIR/artifact 身份或 verifier 失败 |
| `MPF0009` | frontend/backend extension conformance 失败 |
| `MPF0010` | source、token、parser depth、arena、AST/HIR/MIR/LIR 节点、生成输出或 source map 超过 `ResourceLimits`，对应阶段失败关闭 |
| `MPF1000`/`MPF1001`/`MPF1002`/`MPF1005` | 表达式 lexer：缺少语言、非法字符、字符串或数字字面量错误 |
| `MPF1010`—`MPF1015` | 表达式 parser：缺失/多余表达式、分隔符、member 和调用参数顺序错误 |
| `MPF1110` | 当前源语言不支持需要单次求值 lowering 的链式比较；Python 已使用专用 comparison-chain AST |
| `MPF1111` | Python 条件表达式缺少必需的 `else` 分支 |
| `MPF1200` | 当前 statement grammar 不支持或结构无效；完整 grammar 阶段将继续细分 |
| `MPF1201` | 请求的语言标准超出 frontend manifest 范围，或某语法 feature 在所选旧标准中尚不可用 |
| `MPF13xx`—`MPF19xx` | 各语言 logical-source 与 statement lexer，详见下文 |
| `MPF20xx` | 名称、类型、控制流、容器、函数关联及目标能力错误 |
| `MPF2101` | 控制流已经终止后的不可达语句 warning |

Fortran source-form 诊断使用 `MPF13xx`：`MPF1301`/`MPF1302` 表示 continuation 边界错误，`MPF1303` 表示尚未支持的预处理指令，`MPF1304`/`MPF1305` 表示续行字符常量错误，`MPF1306`/`MPF1307` 表示 fixed-form 列布局错误。

Python logical-source 诊断使用 `MPF14xx`：`MPF1401`/`MPF1402` 表示分隔符不匹配或未闭合，`MPF1403` 表示反斜杠续行边界无效，`MPF1404` 表示单/双引号字符串未闭合，`MPF1405` 明确拒绝当前尚未建模的 triple-quoted string。

Matlab logical-source 诊断使用 `MPF15xx`：`MPF1501`/`MPF1502` 表示分隔符不匹配或缺少 `...`，`MPF1503` 表示字符串未闭合，`MPF1504` 表示 block comment 未闭合，`MPF1505` 表示 ellipsis continuation 到达文件末尾。

Python statement lexer 诊断使用 `MPF16xx`：`MPF1601` 表示规范化边界后仍出现未闭合字符串 token，`MPF1602` 表示 statement token stream 中出现非法控制字符。语法结构暂沿用 `MPF1200` unsupported/invalid statement 契约；迁移至完整 Python grammar 时将按产生式细分 parser code。

Matlab statement lexer 诊断使用 `MPF17xx`：`MPF1701` 表示规范化边界后仍出现未闭合字符串 token，`MPF1702` 表示非法控制字符。当前 Matlab 结构语法同样沿用 `MPF1200`，完整 grammar 阶段再按产生式细分。

Fortran statement lexer 诊断使用 `MPF18xx`：`MPF1801` 表示 source-form normalization 后仍出现未闭合字符串 token，`MPF1802` 表示非法控制字符。Fortran parser 对 declaration attribute、非恒定 shape 和未支持产生式继续使用 `MPF1200` 失败关闭。

TypeScript statement lexer 诊断使用 `MPF19xx`：`MPF1901` 表示 block comment 未闭合，`MPF1902` 表示单/双引号字符串未闭合，`MPF1903` 表示非法控制字符。template literal、loose equality、`var`/arrow、nested function、尚未建模的 module/object/`for of`/`for in` 产生式使用 `MPF1200` 失败关闭；版本范围使用 `MPF1201`。canonical `for` 会拒绝 `const` induction、非布尔条件或更新其他 binding；initializer 对 induction binding 遵循 TDZ/use-before-assignment，离开 lexical block/loop 后的名称按 `MPF2001` 未定义处理，非整数 `number` 下标按 `MPF2023` 拒绝。

## 语义与目标能力诊断

| 代码 | 含义 |
|---|---|
| `MPF2001` | 名称未定义 |
| `MPF2002` | 运算符与操作数类型不兼容 |
| `MPF2003` | 变量在确定赋值前被读取 |
| `MPF2004` | 函数结果没有在要求的路径上确定赋值 |
| `MPF2005`/`MPF2006` | range/counting loop step 为零或 Python `range` 参数不是整数 |
| `MPF2007`—`MPF2009` | C++ 无法表示变量类型变化、不兼容返回类型或 value/empty/fallthrough 混合返回 |
| `MPF2010`—`MPF2012` | break/continue/return 所处控制流上下文无效 |
| `MPF2020` | 容器 element type、矩形性、nesting rank 或目标静态表示不兼容 |
| `MPF2021`—`MPF2025` | 静态越界、非容器索引、索引/边界类型、shape 不匹配或索引 rank/语法无效 |
| `MPF2026`—`MPF2028` | 数组 intrinsic 参数数量、extent/目标大小或 reshape/dimension contract 无效 |
| `MPF2029`/`MPF2030` | slice/colon 使用上下文无效或 step 为零 |
| `MPF2031` | slice/section replacement kind、长度或 shape 不 conform |
| `MPF2032` | C++ 无法静态统一 Python `and/or` 的结果类型 |
| `MPF2033` | 当前 Python `float` 子集的参数数量或类型无效 |
| `MPF2034` | 函数输入/多输出数量或 required association 无效 |
| `MPF2035` | C++ 递归返回类型无法静态表示 |
| `MPF2036` | Fortran INTENT 或确定赋值契约无效 |
| `MPF2037` | Fortran FUNCTION/SUBROUTINE 调用形式不匹配 |
| `MPF2038` | writable actual 不可定义，或多个 writable actual 可能共享根 storage |
| `MPF2039` | Fortran dummy/actual scalar-array、rank、extent、element type 或 assumed-shape 位置不匹配 |
| `MPF2040` | Fortran keyword/OPTIONAL/PRESENT association contract 无效 |
| `MPF2041` | Python parameter kind/keyword association，或启用默认参数语义的前端违反安全 default contract |
| `MPF2042` | Python assignment pattern、固定 sequence 或普通/starred target 数量不匹配 |
| `MPF2043` | Matlab `switch/case` 或 Fortran `SELECT/CASE` 的 selector、case 类型、范围或选择关系无效 |
| `MPF2044` | Python ordering 操作数不兼容，或 `cpp` 无法静态保持 comparison-chain、sequence identity、conditional-expression 等目标类型/对象语义组合 |
| `MPF2045` | Python identity 使用不可移植的数值/string 对象驻留语义，或 membership container/string needle 不在当前 string/list/tuple 可保持边界内 |
| `MPF2046` | Matlab 数组运算的 operand type、rank、shape 不相容，或矩阵求解/幂超出当前静态稠密实数与 safe-integer 方阵指数边界；矩形数值秩亏由生成 runtime 返回基本最小二乘解并警告，方阵在 diagonal/upper/lower/dense 结构路径中对精确奇异或近奇异条件警告后继续 |
| `MPF2047` | Matlab 转置的 operand 超出当前 vector/rank-2、非 complex/character-array 可保持边界 |
| `MPF2048` | Matlab `end` 不在索引上下文中，或无法绑定有效的 axis/linear extent 来源 |
| `MPF2049` | Matlab 线性/逐维 logical selector 的 shape/type 或 indexed replacement shape 不符合当前广义 selector contract |
| `MPF2050` | Matlab shape-changing assignment 违反 null-assignment/shape contract，例如非 vector 线性删除、多个非 colon selector，或缺失维度的 shape-changing selector |

语义分析和 capability validator 必须在 emitter 前产生这些错误；失败结果不应包含可被误认为成功输出的目标代码。新增或重新定义稳定 code 时必须同步本表、测试和 changelog。
