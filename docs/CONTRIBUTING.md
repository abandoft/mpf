# 参与开发

提交应围绕一个可验证的语言能力或基础设施里程碑，包含实现、成功/失败测试、支持矩阵和 TODO 状态更新。当前生产实现使用 C++17；C++ 输出目标的代码身份必须保持为 `cpp`。

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Linux/macOS 上还应运行：

```sh
cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers
```

提交前的工程质量检查：

```sh
cmake --preset quality
cmake --build build/quality --target mpf-format-check
cmake --build --preset quality

cmake --preset coverage
cmake --build --preset coverage
```

修改 `.github/workflows/` 时还应运行与 `Quality / actionlint` 相同的检查：

```sh
go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12 -color
```

`quality` 对全部项目 C++ 源码执行零告警 clang-tidy；`coverage` 运行完整 CTest，生成
`build/coverage/coverage/html/` 和 `summary.json`，并强制生产代码行覆盖率不低于 85%。需要重写格式时运行
`cmake --build build/dev --target mpf-format`。

不要在根目录或源码子目录中生成构建文件；只使用 `build/<name>/`。不要修改、构建或引用废弃归档内容。公共 API 变更直接更新当前 producer、consumer、测试和文档，不保留旧接口适配层。新增源语言语义必须在对应 frontend 中解析并 lowering 到公共 IR，不得在任一目标 emitter 中根据源文本猜测。语言能力原则上必须同时提供 JavaScript 和 C++17 后端测试；C++17 生成物测试必须真实编译。生成任一目标不得依赖另一目标的生成物。

目标后端源码分别位于 `src/backends/javascript/` 和 `src/backends/cpp/`，文件名只表达
`backend`、`lir`、`lowering`、`renderer` 等职责，不重复目标语言前缀。跨目标公共组件才能放在
`src/backends/` 根目录；任一目标目录不得包含另一目标的头文件。

版本号遵循 [版本策略](VERSIONING.md)：从 `0.0.1` 开始，patch 只使用 `0`—`9`，
因此 `0.2.9` 的下一个版本是 `0.3.0`。只修改 CMake `project(VERSION)`；公共头、CLI 和安装包
版本均由配置阶段生成，禁止再次手写副本。
