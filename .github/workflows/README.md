# GitHub Actions 职责矩阵

每个 workflow 只拥有一个失败域。分支保护应按下表的 workflow/job 名称配置 required
checks，不再依赖历史上责任混杂的 `ci.yml`。

| Workflow | 唯一职责 | 触发时机 |
|---|---|---|
| `Validation / Fast` | Linux GCC 构建、核心测试、生成 C++ 编译契约和安装布局 | 每次 push/PR |
| `Compatibility` | GCC、Clang、macOS 和 MSVC 上的平台/语言 CTest 与全量差分（性能另立） | 每次 push/PR |
| `Quality` | clang-format 与 clang-tidy/warnings-as-errors | 每次 push/PR |
| `Sanitizers` | ASan/UBSan 下的 compiler、unit/integration 和 fuzz-smoke 路径 | push/PR + 每周 |
| `Coverage` | 生产源码覆盖率报告和 85% 硬门槛 | 每次 push/PR |
| `Performance` | 版本化延迟/吞吐/内存/产物大小发布门禁 | push/PR + 每周 |
| `Security` | CodeQL 与 PR 依赖审查 | push/PR + 每周 |
| `Release` | 经测试的跨平台包、SHA-256 和 GitHub Release 发布 | 版本 tag/手动 |

Fortran 源程序 oracle 有意使用 reference 工具链实际支持的上一代严格模式，当前为
`f2018`。这不会重新定义 MPF 的 Fortran 2023 frontend 目标；生成 JavaScript/C++ 仍与源程序及
声明式 oracle 直接比较。

所有 workflow 使用最小权限 token、workflow/ref 级并发取消、显式超时和短期失败产物。coverage 和
performance 报告作为发布证据，保留时间更长。
