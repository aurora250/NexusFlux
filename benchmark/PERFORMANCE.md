# NexusFlux 内容渲染性能

`content_benchmark.cpp` 走完整渲染路径：

```text
snapshot/inventory → build_content() → cell_grid → DOM panel/rich_text
→ tui::headless render → plaintext
```

覆盖 11 个页面 × 120×40 / 160×48，进程快照为 300 行。

## 构建与运行

```powershell
cmake -S . -B build -DNEXUSFLUX_NEXUSFORCE_MODE=source -DNEXUSFLUX_BUILD_BENCHMARKS=ON
cmake --build build --target nexus-flux-benchmark -j 8
.\build\benchmark\nexus-flux-benchmark.exe
```

在 Windows 上需要把 `NexusForce.dll` 放在 benchmark 可执行文件同目录（CMake 的 deploy 步骤会处理）。

## 参考结果

Windows 11 / 32 核 / Release / MSVC 19.44，单次运行，仅用于同机回归对比：

| 页面       | 120×40 (ms) | 160×48 (ms) |
|------------|------------:|------------:|
| overview   |        2.67 |        3.86 |
| processes  |        2.47 |        3.48 |
| gpu        |        1.88 |        2.79 |
| storage    |        2.04 |        2.88 |
| network    |        1.84 |        2.88 |
| system     |        2.43 |        3.39 |
| containers |        1.92 |        2.92 |
| services   |        1.92 |        2.91 |
| history    |        2.36 |        4.60 |
| events     |        1.80 |        3.95 |
| explorer   |        2.07 |        4.00 |

> 这些数字包含进程/清单构造、内容构建、DOM 渲染和 plaintext 转换，不包含系统采样与终端 I/O。
