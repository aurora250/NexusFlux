# NexusFlux

A NexusForce-based local CPU, GPU, process, storage, network, container and service monitoring.

The executable is `nexus-flux` with the short alias `neflux`.

## Usage

```sh
nexus-flux                                   # interactive monitor
nexus-flux gpu --interval 1000               # initial page and interval
nexus-flux snapshot                          # one JSON snapshot
nexus-flux sample --samples 10 > samples.jsonl
nexus-flux record session.jsonl              # record while monitoring
nexus-flux replay session.jsonl              # replay a recording
nexus-flux export csv metrics.csv --samples 10
nexus-flux export svg screen.svg --samples 30
nexus-flux gpu select --count 1 --min-free-mib 8192
nexus-flux network connections --filter port:443 --sort download --json
nexus-flux doctor --json
```

## Pages and controls

| Key                             | Page                                                                     |
|---------------------------------|--------------------------------------------------------------------------|
| `1`–`8`, `←/→`, `Tab/Shift+Tab` | Overview, Processes, GPU, Storage, Network, System, Containers, Services |
| `↑/↓`, `PgUp/PgDn`, `Home/End`  | Select or scroll                                                         |
| `F3`, `f`, `/`                  | Inline filter                                                            |
| `F4`, `S`, `r`                  | Sort chooser; next sort key; reverse                                     |
| `F5`                            | View chooser                                                             |
| `C`                             | Process column chooser                                                   |
| `g`, `u`, `i`                   | GPU-only, own-user-only, loopback toggle                                 |
| `F1`, `?`, `:`                  | Help, menu, command list                                                 |
| `F2`, `c`, `w`                  | Settings, theme, save configuration                                      |
| `Enter`, `d`, `F6`              | Details                                                                  |
| `F7`, `l`                       | Readable process output files                                            |
| `e`, `o`                        | Process environment / open files                                         |
| `k`/`K`, `t`, `s`, `n`, `a`     | Kill, terminate, signal, nice, affinity                                  |
| `R`                             | ROS graph browser with topic/node/service drill-down                     |
| `Space`, `v`, `V`, `U`          | Mark process/subtree/clear                                               |
| `W`, `-`, `+`                   | Time window and sampling interval                                        |
| `X`                             | System Explorer (extras and capabilities)                                |
| `0`, `E`                        | History and alert Events views                                           |
| `[`, `]`                        | Previous/next GPU                                                        |
| `q`, `Ctrl+C`, double `Esc`     | Quit                                                                     |

Filters support `field:value`, `!` negation, numeric comparisons, `/regex/`, space-separated
AND and ` | ` OR, for example `user:alice cpu:>10 !name:rust` or `name:/python|torch/ pid:42 | mem:>1024`.

## License

MIT. See [LICENSE](LICENSE).
