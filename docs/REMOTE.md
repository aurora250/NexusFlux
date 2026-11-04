# Remote machines and the agent

NexusFlux can render and record telemetry from another machine without a browser or
an inbound listening port. The same `nexus-flux` binary runs as a hidden agent on the
target; the local process is the server.

```text
nexus-flux (UI / record / export)
    │  ssh -T
    ▼
nexus-flux agent --stdio (guest)
    │
    └─ local collector / discovery
```

## Default: local

```sh
nexus-flux
nexus-flux snapshot
```

Without `--machine`, the in-process local source is used. No child process is started
and no protocol serialization takes place.

## SSH target

```sh
nexus-flux --machine ssh:user@host:22
nexus-flux --machine ssh:user@host:22 snapshot
```

The guest needs the same version of `nexus-flux` in `PATH`, or pass:

```sh
nexus-flux --machine ssh:user@host --agent-path /usr/local/bin/nexus-flux
```

## QEMU target

QEMU user networking must expose guest SSH:

```text
-netdev user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22
```

List resolvable targets:

```sh
nexus-flux machines
```

Then connect:

```sh
nexus-flux --machine qemu:freebsd-x86_64 --machine-user freebsd
```

The VM must already be running. For a libvirt domain, `--machine-start` calls
`virsh start` first:

```sh
nexus-flux --machine qemu:my-domain --machine-user root --machine-start
```

For an unmanaged QEMU process, provide an explicit argv:

```sh
nexus-flux --machine qemu:guest --machine-user user \
  --machine-start --machine-start-command /usr/bin/qemu-system-x86_64 \
  --machine-start-arg -machine --machine-start-arg q35 ...
```

NexusFlux never invokes a shell for this. Without a start command, an unmanaged QEMU
process must already be running with the same `hostfwd` configuration.

## Agent

`agent` is intentionally not listed in `--help`:

```sh
nexus-flux agent --stdio
nexus-flux agent --stdio --read-only
nexus-flux agent --stdio --allow-actions
```

- stdin/stdout carry the protocol; logs go to stderr.
- The server sends `configure` with the collection policy.
- The agent streams snapshots at the configured interval.
- Remote process detail defaults to `normal` (command lines are truncated); local
  collection remains `full`.
- Remote actions are disabled unless both sides opt in:
  `--allow-actions` on the server and `--allow-actions` on the agent.

## Platform notes

### Windows

Install the OpenSSH client (`ssh.exe`). Run the Windows build of `nexus-flux agent`
inside the Windows guest. A remote Windows agent is collected over SSH the same way
as Linux.

### macOS

The server and agent build with the macOS NexusForce build. Reading other processes
requires the binary to be code-signed and granted the relevant privacy permission
(for example Full Disk Access or Accessibility, depending on the collected fields).
Unsigned/ad-hoc builds can still collect CPU, memory, disk and network; process
fields may be unavailable. See Apple's TCC documentation before deploying.

### FreeBSD

Build the FreeBSD target with the same source tree and install it in the jail/VM.
`launchd` is replaced by `rc.d` service discovery; process, CPU, memory and network
data come from NexusForce's FreeBSD `sysinfo`/`process` backends.

### Linux

No special setup. The existing local collector and ASan smoke test remain unchanged.

## Action matrix

| Action | Local | Remote |
|---|---|---|
| signal | yes | yes with `--allow-actions` |
| nice | yes | yes with `--allow-actions` |
| affinity | yes | yes with `--allow-actions` |
| process logs / files / environment | yes | disabled (agent RPC not implemented) |
| container/service actions | yes | disabled |
| VM control | yes | disabled |
| packet capture | yes | disabled |
| GPU select | yes | disabled |

Unsupported remote entries are hidden or return `not supported by agent`; they never
fall back to the local machine.

## Security

- The agent does not listen on a TCP port.
- SSH uses `BatchMode=yes`; use keys or an SSH agent, never a password on the command line.
- Host keys follow the user's normal SSH configuration.
- Remote mode is read-only by default.
- Actions are validated again by the agent; they are not forwarded as local PIDs.
- A malformed or oversized protocol line terminates the session.

## Troubleshooting

| Symptom | Check |
|---|---|
| `QEMU machine not found` | `nexus-flux machines`; use the exact name, including the PID suffix |
| `no SSH hostfwd` | Add `hostfwd=tcp:...-:22` to the QEMU `-netdev user` argument |
| `requires --machine-user` | Pass the guest SSH user |
| `agent handshake timeout` | Run `ssh user@host -- nexus-flux agent --stdio` manually |
| `actions disabled` | Start both sides with `--allow-actions`; check `--read-only` |
| Large remote snapshot | Use the default `normal` process detail; `--full-sampling` is heavier |
