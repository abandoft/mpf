# Differential corpus

`corpus.cmake` 是可执行语义语料的唯一清单。每个 case 声明名称、源语言、输入文件、输出 oracle，以及可选的输出归一化模式。

CTest 的 `mpf.differential.*` runner 对每个 case 执行：

1. 生成 JavaScript，由 Node.js 做语法检查和执行；
2. 生成 C++17，使用顶层构建选择的同一个 generator/compiler 严格编译并执行；
3. Python case 在解释器可用时直接执行；
4. Fortran case 在 gfortran 可用时以可配置的严格标准模式（当前默认 `-std=f2018`）及 `-Wall -Wextra -Werror` 编译执行；
5. 直接比较所有可用路径，并再次与 corpus oracle 比较。

每次运行在 `build/<preset>/differential/<case>/` 保存生成源码、嵌套 C++ 构建树和 `differential-result.txt`。Portability workflow 在失败时上传结果文件与生成源码，可直接检查路径差异。

输出模式：

- `tokens`：将空白序列归一化，适合 Fortran list-directed 数值输出；
- `lines`：保留行内空白，仅统一换行和行尾空白，适合字符串敏感语料。

缺少 Node/Python 时，本地默认构建会运行其余可用路径；CI 使用 `MPF_REQUIRE_DIFFERENTIAL_RUNTIME=ON`，确保 Node.js 与 Python 不会被静默跳过。普通 Matlab case 比较 JavaScript、C++17 与 oracle，不能称为真实 Matlab 执行。独立的手动/可复用 `Matlab Reference` workflow 接入官方 MathWorks R2024b runner，先对 `matlab-invocation-context` 执行原始源码并与两端实际结果比较；同时采集仍待实现的输出边界。具体证据与范围见 [native reference 指南](../reference/matlab/README.md)，该接入必须经实际远程运行验证，不能用本地 synthetic verifier 测试代替。

Matlab argument range/representation case 依据官方 [`mustBeInRange`](https://www.mathworks.com/help/matlab/ref/mustbeinrange.html)、[`gt`](https://www.mathworks.com/help/matlab/ref/double.gt.html) 和 [`length`](https://www.mathworks.com/help/matlab/ref/double.length.html) 合同建立 oracle：区间默认包含上下界，显式 flag 可以排除上下端，numeric 关系比较使用实部；空数组的 length 是 0，不能用最大非零 extent 作为 length。旧 empty/sparse-zero/sparse-indexing oracle 已据此校正。
