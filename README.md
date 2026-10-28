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
nexus-flux export svg screen.svg --input recording.jsonl   # renders replay history
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
- `O` cycles the configured overview box presets.
- `Enter` / `d` opens process details.

Overview boxes are comma-separated lists in `overview_presets`, with the active
index in `overview_preset`. Examples: `"cpu,memory,gpu,storage,network,processes"`,
`"cpu,gpu,processes"`, and `"cpu,memory,storage,network,processes"`.

### Processes

![Processes](docs/images/processes.png)

Full process table with configurable columns, filtering, grouping, tree view and
per-process CPU/RAM/GPU/I/O columns.

- `2` opens Processes.
- `F3` / `f` filters; `F4` / `S` / `r` sorts.
- `C` opens the column chooser.
- `T` toggles the tree; click a tree prefix to collapse or expand a node.
- `D` toggles the persistent process side panel.
- Mouse wheel scrolls the table viewport; `↑/↓` moves the selected process.
- `d` / `Enter` opens the detailed process panel.
- `P` pauses the process list and retains the last CPU/RAM/GPU values.
- `F` follows the selected process.
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
depth. Filesystem and block-device selections are independent; the throughput chart
follows the selected block device and keeps a separate read/write history per device.

- `4` opens Storage.
- `N` switches between the filesystem and block-device tables.
- `A` returns the throughput chart to all physical devices.
- `↑/↓` selects a row in the active table; clicking a row also selects it and switches the chart to that device.
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
ports. `HOST & POWER` also shows the hardware inventory: CPU family/model/stepping,
extended family/model, package, core name, instructions and SIMD extensions, L1/L2/L3
caches, BIOS brand/version/date/microcode, motherboard vendor/model/chipset/southbridge,
and memory type/channel/size/controller speed/LLC/timings. The values come from
`neforce::sysinfo`; unsupported fields are shown as `—`.

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

Services with scope, active/sub state, PID, CPU, memory, tasks and description.
Linux uses systemd; Windows uses SCM.

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
- Mouse wheel scrolls the series list; `↑/↓` moves the selected series; click a row to select it.
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
- Mouse wheel scrolls the settings list.
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

- `d` / `Enter` opens details; `F6` opens the tools menu.
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

The theme chooser lists built-in and `.theme` files. The highlighted theme is applied immediately;
`Esc` returns to the main menu. 
Put `.theme` files in `~/.config/neflux/themes` (Linux), `%APPDATA%\neflux\themes` (Windows),
set `NEFLUX_THEME_DIR`, or point `theme_file = "…"` at a specific file.
Themes may override CPU/memory/network/GPU/temperature/process gradient stops,
`graph_text`, `meter_bg`, followed/pause/banner attributes, plus the base palette.
`graph_gradient` enables vertical graph gradients.

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
| `F6`                            | Context tools (details/logs/signals/priority/container/service actions)  |
| `F7`                            | Logs: process output, container logs, service journal                    |
| `Shift+←/→`                     | Horizontal process-column window                                         |
| `,` / `.`                       | Replay: previous / next frame                                            |
| `F1`, `?`, `:`                  | Help, menu, command list                                                 |
| `F2`, `c`, `w`                  | Settings, theme chooser, save configuration                              |
| `Enter`, `d`                    | Details                                                                  |
| `Right click`                   | Context actions on the active page                                       |
| `Ctrl+C` / `Ctrl+Shift+C`       | Copy selection (OSC52/clipboard); quit when nothing is selected          |
| `q`, double `Esc`               | Quit                                                                     |

The process page exposes 90 selectable columns (`C`); `--read-only` and replay reject
all state changes (signals, nice/affinity, container/service lifecycle) while keeping
inspection and copy available.

Filters support `field:value`, `!` negation, numeric comparisons, `/regex/`, space-separated
AND and ` | ` OR, for example `user:alice cpu:>10 !name:rust` or `name:/python|torch/ pid:42 | mem:>1024`.

## Configuration

Configuration is read from `$XDG_CONFIG_HOME/neflux/config.toml` (falling back to
`~/.config/neflux/config.toml`). On Windows it lives under `%APPDATA%\neflux\config.toml`.

## Logs

At startup NexusFlux initializes `neforce::logger` with a rotating file sink:

- Linux: `~/.config/neflux/logs/nexus-flux.log.<date>`
- Windows: `%APPDATA%\neflux\logs\nexus-flux.log.<date>`

Windows writes a minidump next to the log on an unhandled exception; fatal signals
on Linux are also recorded. A small `crash-fallback.log` is written if the logger
itself cannot flush during a crash.

## Building

NexusForce is the only external dependency. Let CMake obtain it with `NEXUSFLUX_NEXUSFORCE_MODE`:

| Mode             | Behaviour                                                                                                  |
|------------------|------------------------------------------------------------------------------------------------------------|
| `auto` (default) | Use `.deps/NexusForce`, then an explicit build tree, then an installed package. Never compiles NexusForce. |
| `source`         | Compile the NexusForce checkout as part of the NexusFlux build.                                            |
| `prebuilt`       | Only `.deps/NexusForce` or `NEXUSFLUX_NEXUSFORCE_BUILD_DIR`.                                               |
| `package`        | Only `find_package(NexusForce 1.0 CONFIG)`.                                                                |

Package mode also needs NexusForce's own dependencies (OpenSSL, PCRE2) to be
discoverable; when they live in vcpkg, add
`-DCMAKE_PREFIX_PATH=<vcpkg>/installed/x64-windows`.

## License

MIT. See [LICENSE](LICENSE).
