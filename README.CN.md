# NexusFlux

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

## 页面与按键

| 按键                            | 页面                                          |
|---------------------------------|-----------------------------------------------|
| `1`–`8`、`←/→`、`Tab/Shift+Tab` | 概览、进程、GPU、存储、网络、系统、容器、服务 |
| `↑/↓`、`PgUp/PgDn`、`Home/End`  | 选择或滚动                                    |
| `F3`、`f`、`/`                  | 行内过滤                                      |
| `F4`、`S`、`r`                  | 排序菜单；下一个排序键；反向                  |
| `F5`                            | 视图菜单                                      |
| `C`                             | 进程列选择                                    |
| `g`、`u`、`i`                   | 仅 GPU、仅当前用户、回环开关                  |
| `F1`、`?`、`:`                  | 帮助、菜单、命令列表                          |
| `F2`、`c`、`w`                  | 设置、主题、保存配置                          |
| `Enter`、`d`、`F6`              | 详情                                          |
| `F7`、`l`                       | 读取进程输出文件                              |
| `e`、`o`                        | 进程环境 / 打开文件                           |
| `k`/`K`、`t`、`s`、`n`、`a`     | 强制结束、终止、信号、nice、CPU 亲和性        |
| `R`                             | 选中进程的 ROS 图浏览与话题/节点/服务下钻     |
| `Space`、`v`、`V`、`U`          | 标记进程/子树/清除                            |
| `W`、`-`、`+`                   | 时间窗口与采样间隔                            |
| `X`                             | 系统扩展能力浏览（extras）                    |
| `0`、`E`                        | 历史曲线与告警事件页面                        |
| `[`、`]`                        | 上一个/下一个 GPU                             |
| `q`、`Ctrl+C`、双击 `Esc`       | 退出                                          |

过滤表达式支持 `field:value`、`!` 取反、数值比较、`/正则/`、空格 AND 与 ` | ` OR，
例如 `user:alice cpu:>10 !name:rust` 或 `name:/python|torch/ pid:42 | mem:>1024`。

## 许可证

MIT，详见 [LICENSE](LICENSE)。
