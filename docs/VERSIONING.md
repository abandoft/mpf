# 版本策略

MPF 的开发版本从 `0.0.1` 开始，使用三段十进制版本号 `MAJOR.MINOR.PATCH`。在 0.x 开发期，`PATCH` 只使用一位十进制数 `0`—`9`；递增 `0.x.9` 时必须进位到 `0.(x+1).0`。例如：

- `0.0.1` 是首个开发版本；
- `0.0.9` 的下一个版本是 `0.1.0`；
- `0.2.9` 的下一个版本是 `0.3.0`；
- 当前开发版本是 `0.6.2`；下一个版本是 `0.6.3`。

CMake `project(VERSION)` 是源码树的唯一版本源。配置阶段由它生成公开 `mpf/version.hpp`、CMake package version 和 CLI 版本；测试不得复制硬编码版本字符串。配置会拒绝 `0.0.0` 以及 patch 大于 9 的版本。

## 0.x 开发快照策略

MPF 尚未形成可发布产品。每个 0.x 版本都是一个精确的开发快照，只定义该源码树当前唯一的 API、ABI、CLI、CMake package、descriptor 和 schema contract，不承诺兼容任何更早 MPF 版本：

- CMake package 使用 `ExactVersion`；consumer 必须写出当前完整版本，旧版本请求必须配置失败；
- 破坏性重构直接替换当前接口，不保留 deprecated overload、旧名称 alias、旧包变量、兼容 facade、双写字段或 migration shim；
- descriptor/API/schema 中的版本号用于验证当前数据身份，不表示实现能够读取历史格式；contract 变化时同步更新 producer、consumer、verifier、golden、fuzz、性能基线和文档；
- 正式的稳定 C/C++ API/ABI、动态插件协商和数据迁移策略只能在产品完成并明确进入相应稳定里程碑后建立。

Matlab、Python、Fortran 和 TypeScript 的旧语言标准支持是产品输入语义，不属于旧 MPF 兼容层；相关版本 gate 继续按语言 manifest 和官方 grammar 建设。

Git tag 直接使用 `MAJOR.MINOR.PATCH`，不添加 `v` 前缀，并且正式发布只接受位于 `main` 历史上的 annotated tag。`CHANGELOG.md` 和 `CHANGELOG-ZH.md` 不设置待发布占位段，必须直接以当前 CMake 项目版本标题开头且条目数一致。GitHub Release 正文由 Bash 脚本提取 `CHANGELOG.md` 中当前标签版本标题下、下一个版本标题前的内容，不自动生成通用 release notes。changelog 保存开发快照的工程变化，但任何历史条目都不构成跨版本兼容承诺。发布检查必须保证 tag、CMake、CLI、安装包、性能基线和 changelog 版本完全一致；七类 canonical workflow 会在标签 SHA 上重新执行，随后才允许三平台候选测试、安装后 consumer 验证、制品校验、来源证明、发布和公开资产回验。

每个版本的 changelog 应整理为 **8—20 条**用户可理解、可独立验证的更新。达到 8 条即可形成新版本；超过 20 条时应拆分版本或合并过细条目。条目数量不替代测试、覆盖率、性能和制品门禁。Release workflow 会拒绝条目数量不合规、标题不在文件首行或版本身份不一致的源码树。
