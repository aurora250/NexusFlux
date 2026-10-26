# NexusFlux

English | [简体中文](README.CN.md)

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

## Interface guide

### Overview

![Overview](docs/images/overview.png)

Landing page with the host summary, CPU graph and per-core history, memory, GPU,
storage and network cards, plus a process preview.

- `1` opens Overview.
- `W` changes the chart window; `-` / `+` changes the sampling interval.
- `↑/↓` and `PgUp/PgDn` select processes in the preview.
- `Enter` / `d` opens process details.
- `Enter` / `d` opens process details.

### Processes

![Processes](docs/images/processes.png)

Full process table with configurable columns, filtering, grouping, tree view and
per-process CPU/RAM/GPU/I/O columns.

- `2` opens Processes.
- `F3` / `f` filters; `F4` / `S` / `r` sorts.
- `C` opens the column chooser.
- `T` toggles the tree; click a tree prefix to collapse or expand a node.
- `D` toggles the persistent process side panel.
- `d` / `Enter` opens the detailed process panel.
- `P` pauses the process list; `F` follows the selected process.
- `L` switches lazy/direct CPU sorting.
- `Space` marks a process; `k`/`K`, `t`, `s`, `n`, `a` act on it.
- `e` / `o` show environment and open files; `l` / `F7` reads output files.
- `R` opens the ROS graph for the selected process.
- Right click opens the process action menu.

### GPU

![GPU](docs/images/gpu.png)

GPU cards, utilization/VRAM history, device details and per-process GPU usage.

- `3` opens GPU.
- `[` / `]` select the previous or next GPU.
- `G` opens device details; `D` toggles device details.
- Click a card to select it; click a process row to open that process.
- Right click a card for device actions.
- `--gpu-panel` selects the initial GPU panel.

### Storage

![Storage](docs/images/storage.png)

Filesystem capacity/inode table and block-device throughput, IOPS, latency and queue
depth. Filesystem and block-device selections are independent.

- `4` opens Storage.
- Click either table to select a row.
- `Enter` / `d` opens the selected filesystem or block-device details.
- Right click for details and previous/next row actions.

### Network

![Network](docs/images/network.png)

Download/upload history, interface counters, connections and socket ownership.

- `5` opens Network.
- `N` switches between connections and interfaces.
- `↑/↓` selects; `Enter` opens details.
- `S` cycles the sort key for the active section.
- `i` toggles loopback interfaces.
- `p` opens the process that owns a connection.
- Right click provides filtering and owner actions.

### System

![System](docs/images/system.png)

Host identity, load average, CPU/GPU sensors, pressure stall information and listening
ports.

- `6` opens System.
- `↑/↓` scrolls sensors and listeners.
- `Enter` / `d` opens the selected item where available.

### Containers

![Containers](docs/images/containers.png)

Docker/Podman containers with runtime, state, CPU, memory, PIDs and per-container
network/disk activity.

- `7` opens Containers.
- `F3` / `f` filters.
- `↑/↓` selects; `Enter` opens details.
- Click a row to select it.

### Services

![Services](docs/images/services.png)

systemd services with scope, active/sub state, PID, CPU, memory, tasks and
description.

- `8` opens Services.
- `F3` / `f` filters.
- `↑/↓` selects; `Enter` opens details.
- Click a row to select it.

### System Explorer

![System Explorer](docs/images/explorer.png)

Shortcut-opened view of local extras: NUMA, interrupts, soft IRQs, VM counters,
hardware sensors, RAID, SMART and other probes.

- `X` opens Explorer.
- `↑/↓`, `PgUp/PgDn` scroll.
- `Enter` or double click opens the selected entry.
- The value pane renders JSON without truncating object structure.

### History

![History](docs/images/history.png)

Named metric series collected over time, with a selected-series chart and window
range.

- `0` opens History.
- `↑/↓` selects a series.
- Mouse wheel moves one series at a time; click a row to select it.
- `W` changes the chart window.

### Events

![Events](docs/images/events.png)

Alert events generated from CPU, memory and GPU temperature thresholds. A condition
becomes ACTIVE after `alert_hold_seconds` and is recorded as RECOVERED when it clears.

- `E` opens Events.
- `↑/↓` selects an event.
- Mouse wheel scrolls one event at a time; click a row to select it.
- Configure thresholds and hold time in Settings or `config.toml`.

### Settings

![Settings](docs/images/settings.png)

- `F2` opens Settings.
- Click a row or press `Enter` to open its choice popup.
- `←/→` adjusts a value directly; `↑/↓` moves between rows.
- `w` saves configuration.
- `Esc` returns to the main menu when Settings was opened from it.

### Help and commands

![Help](docs/images/help.png)

- `F1` / `?` opens Help.
- `:` opens the command list.
- `↑/↓`, `PgUp/PgDn` scroll; `Esc` / `q` closes.

### Process details

![Process details](docs/images/process-details.png)

The details popup shows command line, credentials, state, CPU/RAM/I/O, GPU, start
time, runtime, history and open files. The `D` side panel keeps a compact version
visible next to the process table.

- `d` / `Enter` / `F6` opens details.
- `D` toggles the side panel on Processes.
- `e` / `o` open environment and open files.

### Main menu and choosers

![Main menu](docs/images/menu.png)

- `Esc` opens the main menu; `Esc` again closes it.
- `c` opens the theme chooser; highlighting a theme previews it.
- `F4` opens Sort; `F5` opens View.
- `C` opens the process column chooser; `s` opens the signal chooser.
- `Enter` activates the highlighted entry; `Esc` returns to the parent menu.

![Theme chooser](docs/images/theme.png)

The theme chooser lists built-in and `.theme` files. The highlighted theme is applied
immediately; `Esc` returns to the main menu.

![Sort chooser](docs/images/sort.png)

![View chooser](docs/images/view.png)

![Process columns](docs/images/columns.png)

![Signal chooser](docs/images/signal.png)

## Shared controls

| Key                             | Action                                                                   |
|---------------------------------|--------------------------------------------------------------------------|
| `1`–`8`                         | Primary pages                                                            |
| `X`, `0`, `E`                   | Explorer, History, Events                                                |
| `←/→`, `Tab/Shift+Tab`          | Change page                                                              |
| `↑/↓`, `PgUp/PgDn`, `Home/End`  | Select or scroll                                                         |
| `F3`, `f`, `/`                  | Inline filter where available                                            |
| `F4`, `S`, `r`                  | Sort chooser; next sort key; reverse                                     |
| `F5`                            | View chooser                                                             |
| `F1`, `?`, `:`                  | Help, menu, command list                                                 |
| `F2`, `c`, `w`                  | Settings, theme chooser, save configuration                              |
| `Enter`, `d`, `F6`              | Details                                                                  |
| `Right click`                   | Context actions on the active page                                       |
| `q`, `Ctrl+C`, double `Esc`     | Quit                                                                     |

Filters support `field:value`, `!` negation, numeric comparisons, `/regex/`, space-separated
AND and ` | ` OR, for example `user:alice cpu:>10 !name:rust` or `name:/python|torch/ pid:42 | mem:>1024`.

## Configuration

Configuration is read from `$XDG_CONFIG_HOME/neflux/config.toml` (falling back to
`~/.config/neflux/config.toml`). On Windows it lives under `%APPDATA%\neflux\config.toml`.

## License

MIT. See [LICENSE](LICENSE).
