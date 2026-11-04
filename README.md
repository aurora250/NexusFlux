# NexusFlux

English | [简体中文](README.CN.md)

A NexusForce-based CPU, GPU, process, storage, network, container, service and virtual-machine monitor for the local host or a remote agent.

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
nexus-flux machines                       # local QEMU targets with SSH endpoints
nexus-flux --machine qemu:guest --machine-user user   # monitor a QEMU guest
nexus-flux --machine ssh:user@host:22 snapshot         # remote snapshot
```

## Interface guide

### Overview

![Overview](docs/images/overview.png)

Landing page with the host summary, CPU graph and per-core history, memory, GPU,
storage and network cards, plus a process preview. The layout adapts to the
terminal: narrow terminals prioritize CPU + memory, medium widths use a 2×2 card
grid, and wide terminals use a four-card row with the process preview at the bottom;
cards fall back to a single row when their height is too small. For large core counts (for
example 32), the CPU panel uses a two-column core grid and shows `CPU TIMES`
user/system/iowait/steal percentages. The GPU process table reuses the PROCESSES
column, selection and detail strategies.

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
per-process CPU/RAM/GPU/I/O columns. The default columns mirror the common monitor
layout: `PID`, `USER`, `CPU`, `MEMORY`, `GPU-MEM`, `GPU`, `READ`, `WRITE`, `STATE`,
`COMMAND`, `TAGS`.

- `2` opens Processes.
- `F3` / `f` filters; `F4` / `S` / `r` sorts.
- `C` opens the column chooser.
- `T` toggles the tree; click a tree prefix to collapse or expand a node.
- `D` toggles the process detail side panel; double-clicking a process row also opens it.
- Mouse wheel scrolls the table viewport; `↑/↓` moves the selected process.
- `d` / `Enter` opens the detailed process panel.
- `P` pauses the process list and retains the last CPU/RAM/GPU values.
- `F` toggles the followed-process highlight; list selection always follows item identity.
- `L` switches lazy/direct CPU sorting.
- `Space` marks a process; `k`/`K`, `t`, `s`, `n`, `a` act on it.
- `e` / `o` show environment and open files; `l` / `F7` reads output files.
- `R` opens the ROS graph for the selected process.
- Right-click opens the same process action menu as PROCESSES; `SEND SIGNAL` uses the unified NexusForce `system_signal` send API.

### GPU

![GPU](docs/images/gpu.png)

The GPU page summarizes driver/CUDA/NVML versions, device and process counts, and shows
utilization, VRAM, temperature, power/limit, fan, P-state, core/memory clocks and process
count per GPU. Device details also include VBIOS, PCIe link generation/width and maximum
clock; the GPU process table reuses the PROCESSES column configuration, default columns,
selection and detail strategies, with GPU-MEM/GPU supplied by GPU telemetry. The
Intel/AMD DRM backend collects per-process memory and engine usage from `fdinfo`.

- `3` opens GPU; the Overview GPU card separates GPU load and VRAM into independent trends.
- `[` / `]` select the previous or next GPU.
- `G` opens device details; `D` toggles device details.
- Click a card to select it; click a process row to select it, and double-click or `Enter` to open the same detail popup as PROCESSES.
- Right-click a card for device actions; right-click a process row to reuse the PROCESSES process menu (details/follow/pause/terminate/signal).
- `--gpu-panel` selects the initial GPU panel.

### Storage

![Storage](docs/images/storage.png)

Filesystem capacity/inode table and block-device throughput, IOPS, latency and queue
depth. Filesystem and block-device selections are independent; read and write throughput
use separate charts and scales, follow the selected block device, and keep a separate
read/write history per device.

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
ports. The `CPU TIMES` panel shows user/system/iowait/steal percentages and bars.
`HOST & POWER` also shows the hardware inventory: CPU architecture, vendor, brand,
clocks, core/thread counts, L1/L2/L3 caches, instructions and SIMD extensions.
Architecture-specific details change with the CPU:

- x86: family/model/stepping, extended family/model, revision, core voltage,
  technology, socket, code name, bus speed, multiplier and TDP.
- ARM: implementer, part, variant, revision and core name.
- RISC-V: ISA, MMU, microarchitecture and vendor/architecture/implementation IDs.
- LoongArch: family, revision, FPU revision, ISA, address sizes, machine, system
  type, watchpoint support and TLB entries.

The panel also shows BIOS brand/version/date/microcode, motherboard
vendor/model/chipset/southbridge, and memory type/channel/size/controller
speed/LLC/timings. CPU detail fields without a value are omitted.

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
- `D` toggles the service detail side panel.
- Right-click a service for details, logs, start/stop/restart and open-main-process actions.

### Virtual machines

![Virtual machines](docs/images/vms.png)

libvirt domains and unmanaged `qemu-system-*` processes, with state, vCPU count,
memory and host PID.

- `9` opens VMs.
- `↑/↓` selects; `Enter` / `d` opens VM details.
- Double-click a VM row to open its single detail/config side panel (hidden by default), showing vCPUs, memory, autostart and power actions. It closes on page switches or a double-click outside the panel; `D` can also toggle it.
- `s` starts, `x` shuts down, `X` forces off, `r` reboots; `a` toggles the autostart draft and `w` applies it.
- `+` / `-` adjusts vCPUs, `[` / `]` adjusts memory; `[ Apply config ]` or `w` writes the config to libvirt.
- Only libvirt domains support configuration writes; unmanaged QEMU processes are read-only for config but still support shutdown/force-off/pause/resume.
- Right-click or `F6` provides start, shutdown, reboot, force-off, pause/resume, config and
  open-QEMU-process actions.

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
time, runtime, history and open files. The `D` side panel scrolls through labels,
command, active table fields and all sampled fields; its fixed lower section shows
Since-selection CPU, RAM, GPU and VRAM charts next to the process table.

- `d` / `Enter` opens the details popup; `F6` opens the tools menu.
- `D` or a double-click on a process row opens the side panel; it narrows to the available width and wraps long field values; the wheel scrolls its upper section; `D`/`Esc` closes.
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

The signal chooser uses NexusForce `system_signal` as the single signal facade.
The send side uses `system_signal::signal`, `send_to_process()`, `signal_name()` and
`signal_number()`; the receive side uses `system_signal::notification`, `subscribe()`,
`wait_for_notification()`, and `enqueue_signal()` / `enqueue_event()`. `event` only
represents non-signal local events. Handlers are installed lazily per subscription and
can chain pre-existing POSIX handlers; `can_send()` exposes platform send capabilities. All signal delivery and reception goes through
`system_signal`; the `process` class no longer exposes signal operations.

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
| `Enter`, `d`                     | Details                                                                  |
| Double-click a table row         | Open that item's detail side panel on the table side                     |
| Single-click another table row   | Select it and update the open side panel                                 |
| Double-click outside the panel   | Close the detail side panel                                              |
| Right click                      | Open the item's control/context menu                                     |
| `Ctrl+C` / `Ctrl+Shift+C`       | Copy selection (OSC52/clipboard); quit when nothing is selected          |
| `q`, double `Esc`               | Quit                                                                     |

The process page exposes 90 selectable columns (`C`); `--read-only` and replay reject
all state changes (signals, nice/affinity, container/service lifecycle) while keeping
inspection and copy available.

Processes, Network, Containers, Services and VMs keep selection by stable identity:
refreshes, sorting, filtering and page switches continue to follow the same item, and
cross-page jumps keep following the target instead of a stale row index.

Filters support `field:value`, `!` negation, numeric comparisons, `/regex/`, space-separated
AND and ` | ` OR, for example `user:alice cpu:>10 !name:rust` or `name:/python|torch/ pid:42 | mem:>1024`.

## Remote machines

`--machine` selects the telemetry source. The default is `local`; `qemu:<name>` resolves
a local QEMU process and its SSH `hostfwd`, and `ssh:[user@]host[:port]` connects to a
manually configured agent. The remote side runs the same binary as a hidden
`nexus-flux agent --stdio` subcommand, streams snapshots over SSH, and performs no
inbound listening. See [docs/REMOTE.md](docs/REMOTE.md) for hostfwd, agent installation,
macOS code signing, FreeBSD and security notes.

Platform-specific collection, connection, GPU, process inspection and packet-capture
code lives under `src/platform/<os>/`. Shared sources contain no platform conditionals.

Remote collection is read-only by default. Start both sides with `--allow-actions` to
enable process actions; remote process command lines use `normal` detail, while local
collection keeps `full`. For an unmanaged QEMU process, use
`--machine-start-command <exe> --machine-start-arg <arg>`; NexusFlux never invokes a shell.

## Configuration

Configuration is read from `$XDG_CONFIG_HOME/neflux/config.toml` (falling back to
`~/.config/neflux/config.toml`) on Linux, macOS and FreeBSD. On Windows it lives under
`%APPDATA%\neflux\config.toml`.

## Logs

At startup NexusFlux initializes `neforce::logger` with a rotating file sink:

- Linux: `~/.config/neflux/logs/nexus-flux.log.<date>`
- Windows: `%APPDATA%\neflux\logs\nexus-flux.log.<date>`
- macOS: `~/.config/neflux/logs/nexus-flux.log.<date>`
- FreeBSD: `~/.config/neflux/logs/nexus-flux.log.<date>`

Windows writes a minidump next to the log on an unhandled exception.
On Linux x86-64/AArch64 an uncaught C++ exception, `terminate`, or
a fatal `SIGABRT`, `SIGSEGV`, `SIGILL`, `SIGFPE` or `SIGBUS` writes an ELF core next to the log:

```text
nexus-flux-YYYYMMDD-HHMMSS-<pid>.core
```

The fatal log entry and `crash-fallback.log` record `core=<path> bytes=<size>`.
The dump keeps up to 4096 memory segments, 4 GiB per segment, and 8 GiB total so large
GPU/VM mappings are not silently dropped.
If the ELF writer cannot create the file, NexusFlux restores the default action and falls back to the kernel `core_pattern`.

## Building

NexusFlux builds for Windows, Linux, macOS and FreeBSD. NexusForce is the only external dependency. Let CMake obtain it with `NEXUSFLUX_NEXUSFORCE_MODE`:

| Mode             | Behaviour                                                                                                  |
|------------------|------------------------------------------------------------------------------------------------------------|
| `auto` (default) | Use `.deps/NexusForce`, then an explicit build tree, then an installed package. Never compiles NexusForce. |
| `source`         | Compile the NexusForce checkout as part of the NexusFlux build.                                            |
| `prebuilt`       | Only `.deps/NexusForce` or `NEXUSFLUX_NEXUSFORCE_BUILD_DIR`.                                               |
| `package`        | Only `find_package(NexusForce 1.0 CONFIG)`.                                                                |

Package mode also needs NexusForce's own dependencies (OpenSSL, PCRE2) to be
discoverable; when they live in vcpkg, add
`-DCMAKE_PREFIX_PATH=<vcpkg>/installed/x64-windows`.

For the ASan smoke test covering `path::children` and the collector's `/proc/<pid>/fd`
path, configure a source-mode build with `-DNEXUSFLUX_BUILD_ASAN_SMOKE=ON` and run
`ctest -R NeFluxAsanPathChildren`. Disable `NEXUSFORCE_USING_MEMORY_POOL` in that
build (`-DNEXUSFORCE_USING_MEMORY_POOL=OFF -DNEXUSFORCE_MEMORY_POOL_GLOBAL_OVERRIDE=OFF`)
so ASan observes the container allocations directly.

## License

MIT. See [LICENSE](LICENSE).
