# Differential corpus

`corpus.cmake` 是可执行语义语料的唯一清单。每个 case 声明名称、源语言、输入文件、输出 oracle，以及可选的输出归一化模式。

CTest 的 `mpf.differential.*` runner 对每个 case 执行：

1. 生成 JavaScript，由 Node.js 做语法检查和执行；
2. 生成 C++17，使用顶层构建选择的同一个 generator/compiler 严格编译并执行；
3. Python case 在解释器可用时直接执行；
4. Fortran case 在 gfortran 可用时以可配置的严格标准模式（当前默认 `-std=f2018`）及 `-Wall -Wextra -Werror` 编译执行；
5. 直接比较所有可用路径，并再次与 corpus oracle 比较。

每次运行在 `build/<preset>/differential/<case>/` 保存生成源码、嵌套 C++ 构建树和 `differential-result.txt`。Compatibility workflow 在失败时上传结果文件与生成源码，可直接检查路径差异。

输出模式：

- `tokens`：将空白序列归一化，适合 Fortran list-directed 数值输出；
- `lines`：保留行内空白，仅统一换行和行尾空白，适合字符串敏感语料。

缺少 Node/Python 时，本地默认构建会运行其余可用路径；CI 使用 `MPF_REQUIRE_DIFFERENTIAL_RUNTIME=ON`，确保 Node.js 与 Python 不会被静默跳过。Matlab 源执行仍等待授权 runner 或明确的 Octave 兼容策略，因此当前 Matlab case 比较 JavaScript、C++17 与 oracle。
