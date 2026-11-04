# Changelog

## [Unreleased]

### Added

- `telemetry_source` abstraction with in-process local source and SSH-backed remote source.
- Hidden `nexus-flux agent --stdio` mode with a streaming NDJSON protocol.
- `--machine local|qemu:<name>|ssh:[user@]host[:port]`, `--machine-user`,
  `--machine-port`, `--agent-path`, `--machine-start`, `--allow-actions`,
  and `nexus-flux machines`.
- QEMU user-net `hostfwd` discovery for SSH endpoints.
- Remote process actions, disabled by default on both server and agent.
- Remote `signal`, `nice` and `affinity` action RPC with explicit capability flags.
- `--machine-start-command` / `--machine-start-arg` for explicit unmanaged QEMU startup.
- `src/platform/{linux,windows,macos,freebsd}` backend for capabilities, load average, service discovery, collection, connections, GPU, process inspection and packet capture.
- All platform conditionals and OS headers moved out of shared sources; `NEFLUX_PLATFORM_*` remains only in the platform definition header.
- macOS and FreeBSD platform detection; FreeBSD `rc.d`, `netstat` connections, CPU/process dedup; macOS `launchd`/`netstat`.
- CLI gating for remote `network connections`/`gpu select`/`network capture`/`network check`.
- Remote logs/files/environment and workload/VM controls are explicitly blocked.
- `docs/REMOTE.md`; README/README.CN remote-machine and platform notes.

### Changed

- Local monitor remains in-process and unclassified by the protocol.
- Remote process command lines default to `normal` detail; local remains `full`.
- Version bumped to 0.2.0.
