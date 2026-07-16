# GitHub Actions 职责矩阵

> 当前流水线已临时停用。GitHub 只会发现 `.github/workflows/` 中的 workflow；需要恢复时，
> 将本目录整体改回 `.github/workflows/`，不要单独复制某个 YAML 文件。

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
| `Security` | 仓库能力探测；能力可用时执行 CodeQL 与 PR 依赖审查 | push/PR + 每周 |
| `Release` | 经测试的跨平台包、SHA-256 和 GitHub Release 发布 | 版本 tag/手动 |

Fortran 源程序 oracle 有意使用 reference 工具链实际支持的上一代严格模式，当前为
`f2018`。这不会重新定义 MPF 的 Fortran 2023 frontend 目标；生成 JavaScript/C++ 仍与源程序及
声明式 oracle 直接比较。

所有 workflow 使用最小权限 token、workflow/ref 级并发取消、显式超时和短期失败产物。coverage 和
performance 报告作为发布证据，保留时间更长。

`Security capability contract` 会通过仓库 API 显式探测 GitHub Advanced Security。公共仓库或已
授权 GHAS 的私有仓库必须执行 CodeQL 和依赖审查；未授权的私有仓库会留下 notice 并跳过仅由该
产品提供的上传能力，不把订阅缺失伪装成代码失败。clang-tidy/Clang analyzer、Sanitizer 和编译器
零告警门禁不依赖 GHAS，始终执行。

Dependabot 将 GitHub Actions 依赖合并为每周一个更新组，避免每个 Action 各建一个 PR 后重复触发
整套跨平台矩阵。
