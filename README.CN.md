# NexusFlux

[English](README.md) | 简体中文

本地 CPU、GPU、进程、存储、网络、容器与服务监控，基于 NexusForce 实现。

可执行文件为 `nexus-flux`，别名 `neflux`。

## 使用

```sh
nexus-flux                                   # 交互式监控
nexus-flux gpu --interval 1000               # 指定初始页面与采样间隔
nexus-flux snapshot                          # 单次 JSON 快照
nexus-flux sample --samples 10 > samples.jsonl
nexus-flux record session.jsonl              # 监控时录制
nexus-flux replay session.jsonl              # 回放录制
nexus-flux export csv metrics.csv --samples 10
nexus-flux export svg screen.svg --samples 30
nexus-flux gpu select --count 1 --min-free-mib 8192
nexus-flux network connections --filter port:443 --sort download --json
nexus-flux doctor --json
```

## 界面说明

### 概览

![概览](docs/images/overview.png)

首页汇总主机信息、CPU 总图与逐核历史、内存、GPU、存储、网络卡片，以及进程预览。

- `1` 打开概览。
- `W` 调整图表时间窗口；`-` / `+` 调整采样间隔。
- `↑/↓`、`PgUp/PgDn` 选择进程预览中的进程。
- `Enter` / `d` 打开进程详情。

### 进程

![进程](docs/images/processes.png)

完整进程表，支持列配置、过滤、聚合、树形视图，以及每进程 CPU/内存/GPU/IO 列。

- `2` 打开进程页。
- `F3` / `f` 过滤；`F4` / `S` / `r` 排序。
- `C` 打开列选择器。
- `T` 切换树形视图；点击树前缀折叠/展开节点。
- `D` 切换持久进程侧栏。
- 鼠标滚轮滚动表格视口；`↑/↓` 移动选中进程。
- `d` / `Enter` 打开详细进程面板。
- `P` 暂停进程列表；`F` 跟随选中进程。
- `L` 切换 CPU lazy/direct 排序。
- `Space` 标记进程；`k`/`K`、`t`、`s`、`n`、`a` 对进程操作。
- `e` / `o` 查看环境与打开文件；`l` / `F7` 读取输出文件。
- `R` 打开选中进程的 ROS 图。
- 右键打开进程操作菜单。

### GPU

![GPU](docs/images/gpu.png)

GPU 卡片、利用率/显存历史、设备详情和每进程 GPU 使用情况。

- `3` 打开 GPU 页。
- `[` / `]` 选择上一张/下一张 GPU。
- `G` 打开设备详情；`D` 切换设备详情。
- 点击卡片选择设备；点击进程行打开对应进程。
- 右键卡片打开设备操作菜单。
- `--gpu-panel` 指定初始 GPU 面板。

### 存储

![存储](docs/images/storage.png)

文件系统容量/inode 表，以及块设备吞吐、IOPS、延迟和队列深度。文件系统与块设备
使用独立选中项；吞吐图跟随当前块设备，并为每个设备保留独立的读写历史。

- `4` 打开存储页。
- `N` 在文件系统表和块设备表之间切换。
- `A` 让吞吐图回到所有物理设备汇总。
- `↑/↓` 在当前表中选择行；点击行也会选中，并把图表切换到该设备。
- `Enter` / `d` 打开当前选中的文件系统或块设备详情。
- 右键打开详情、上一行/下一行操作。

### 网络

![网络](docs/images/network.png)

下载/上传历史、接口计数器、连接和 socket 所属进程。

- `5` 打开网络页。
- `N` 在连接与接口之间切换。
- `↑/↓` 选择；`Enter` 打开详情。
- `S` 切换当前区域的排序键。
- `i` 切换回环接口显示。
- `p` 打开连接所属进程。
- 右键提供过滤和 owner 操作。

### 系统

![系统](docs/images/system.png)

主机身份、负载、CPU/GPU 传感器、PSI 压力和监听端口。`HOST & POWER` 同时显示硬件档案：
CPU 的 Family/Model/Stepping、扩展 Family/Model、封装、核心名、指令集和 SIMD 扩展、
L1/L2/L3 缓存；BIOS 厂商/版本/日期/微码；主板厂商/型号/芯片组/南桥；以及内存类型、
通道、容量、控制器频率、LLC 和时序。数据来自 `neforce::sysinfo`，平台不支持的字段 显示为 `—`。

- `6` 打开系统页。
- `↑/↓` 滚动传感器和监听列表。
- `Enter` / `d` 在可用时打开选中项详情。

### 容器

![容器](docs/images/containers.png)

Docker/Podman 容器，显示 runtime、状态、CPU、内存、PID，以及每容器网络/磁盘活动。

- `7` 打开容器页。
- `F3` / `f` 过滤。
- `↑/↓` 选择；`Enter` 打开详情。
- 点击行选中。

### 服务

![服务](docs/images/services.png)

服务的 scope、active/sub 状态、PID、CPU、内存、任务数和描述。
Linux 使用 systemd，Windows 使用 SCM。

- `8` 打开服务页。
- `F3` / `f` 过滤。
- `↑/↓` 选择；`Enter` 打开详情。
- 点击行选中。

### 系统浏览器

![系统浏览器](docs/images/explorer.png)

通过快捷键打开的本机扩展信息：NUMA、中断、软中断、VM 计数、硬件传感器、RAID、
SMART 和其他探针。

- `X` 打开系统浏览器。
- `↑/↓`、`PgUp/PgDn` 滚动。
- `Enter` 或双击打开选中条目。
- 右侧内容以 JSON 结构显示，不截断对象层级。

### 历史

![历史](docs/images/history.png)

按名称收集的时间序列，显示选中序列的历史图和窗口范围。

- `0` 打开历史页。
- `↑/↓` 选择序列。
- 鼠标滚轮滚动序列视口；`↑/↓` 移动选中序列；点击行选中。
- `W` 调整图表窗口。

### 事件

![事件](docs/images/events.png)

由 CPU、内存、GPU 温度阈值产生的告警事件。条件持续 `alert_hold_seconds` 后变为
ACTIVE，恢复后记录 RECOVERED。

- `E` 打开事件页。
- `↑/↓` 选择事件。
- 鼠标滚轮一次滚动一项；点击行选中。
- 阈值与保持时间可在设置页或 `config.toml` 中调整。

### 设置

![设置](docs/images/settings.png)

- `F2` 打开设置。
- 点击行或按 `Enter` 打开该项的选择弹窗。
- `←/→` 直接调整数值；`↑/↓` 移动选中行。
- 鼠标滚轮滚动设置列表。
- `w` 保存配置。
- 从主菜单进入设置时，`Esc` 返回主菜单。

### 帮助与命令

![帮助](docs/images/help.png)

- `F1` / `?` 打开帮助。
- `:` 打开命令列表。
- `↑/↓`、`PgUp/PgDn` 滚动；`Esc` / `q` 关闭。

### 进程详情

![进程详情](docs/images/process-details.png)

详情弹窗显示命令行、凭据、状态、CPU/内存/IO、GPU、启动时间、运行时长、历史和
打开文件。`D` 侧栏在进程表旁保留紧凑版本。

- `d` / `Enter` / `F6` 打开详情。
- `D` 在进程页切换侧栏。
- `e` / `o` 打开环境变量和打开文件。

### 主菜单与选择器

![主菜单](docs/images/menu.png)

- `Esc` 打开主菜单；再按 `Esc` 关闭。
- `c` 打开主题选择器；高亮主题即时预览。
- `F4` 打开排序；`F5` 打开视图。
- `C` 打开进程列选择；`s` 打开信号选择。
- `Enter` 激活高亮项；`Esc` 返回父菜单。

![主题选择器](docs/images/theme.png)

主题选择器列出内置主题和 `.theme` 文件；高亮主题立即应用，`Esc` 返回主菜单。
将 `.theme` 文件放到 `~/.config/neflux/themes`（Linux）或 `%APPDATA%\neflux\themes`（Windows），
也可设置 `NEFLUX_THEME_DIR`，或用 `theme_file = "…"` 指定单个文件。

![排序选择器](docs/images/sort.png)

![视图选择器](docs/images/view.png)

![进程列选择](docs/images/columns.png)

![信号选择](docs/images/signal.png)

## 通用按键

| 按键                            | 操作                                          |
|---------------------------------|-----------------------------------------------|
| `1`–`8`                         | 主页面                                        |
| `X`、`0`、`E`                   | 系统浏览器、历史、事件                        |
| `←/→`、`Tab/Shift+Tab`          | 切换页面                                      |
| `↑/↓`、`PgUp/PgDn`、`Home/End`  | 选择或滚动                                    |
| `F3`、`f`、`/`                  | 可用页面的行内过滤                            |
| `F4`、`S`、`r`                  | 排序菜单；下一个排序键；反向                  |
| `F5`                            | 视图菜单                                      |
| `F1`、`?`、`:`                  | 帮助、菜单、命令列表                          |
| `F2`、`c`、`w`                  | 设置、主题选择器、保存配置                    |
| `Enter`、`d`、`F6`              | 详情                                          |
| 右键                            | 当前页面的上下文操作                          |
| `q`、`Ctrl+C`、双击 `Esc`       | 退出                                          |

过滤表达式支持 `field:value`、`!` 取反、数值比较、`/正则/`、空格 AND 与 ` | ` OR，
例如 `user:alice cpu:>10 !name:rust` 或 `name:/python|torch/ pid:42 | mem:>1024`。

## 配置

配置文件路径为 `$XDG_CONFIG_HOME/neflux/config.toml`（回退 `~/.config/neflux/config.toml`）；
Windows 下为 `%APPDATA%\neflux\config.toml`。

## 日志

启动时 NexusFlux 会初始化 `neforce::logger` 并写入轮转日志：

- Linux：`~/.config/neflux/logs/nexus-flux.log.<date>`
- Windows：`%APPDATA%\neflux\logs\nexus-flux.log.<date>`

Windows 未处理异常会另存 minidump；Linux 的致命信号也会记录。如果崩溃时
logger 本身无法刷新，还会写一个 `crash-fallback.log`。

## 构建

NexusForce 是唯一的外部依赖，用 `NEXUSFLUX_NEXUSFORCE_MODE` 选择 CMake 获取它的方式：

| 模式           | 行为                                                                                 |
|----------------|--------------------------------------------------------------------------------------|
| `auto`（默认） | 依次使用 `.deps/NexusForce`、显式指定的构建树、已安装的包；不会自动编译 NexusForce。 |
| `source`       | 把 NexusForce 源码树作为子目录，随 NexusFlux 一起编译。                              |
| `prebuilt`     | 只使用 `.deps/NexusForce` 或 `NEXUSFLUX_NEXUSFORCE_BUILD_DIR`。                      |
| `package`      | 只使用 `find_package(NexusForce 1.0 CONFIG)`。                                       |

package 模式还要求 NexusForce 自己的依赖（OpenSSL、PCRE2、ICU 等）可被发现；
如果它们在 vcpkg 中，加上 `-DCMAKE_PREFIX_PATH=<vcpkg>/installed/x64-windows`。

## 许可证

MIT，详见 [LICENSE](LICENSE)。
