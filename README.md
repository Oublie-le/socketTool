# socketTool

A command-line toolkit for testing TCP / UDP / WebSocket connectivity across
multiple Linux devices.

Built as a **BusyBox-style** multi-call binary: every sub-command is compiled
into the same executable `socketTool` and can be invoked either via
`socketTool <applet>` or via an applet symlink — well-suited to embedded /
resource-constrained Linux devices.

> 📖 **中文文档：[README.zh.md](README.zh.md)**

> Language: C (C99 / POSIX)
> Dependencies: `pthread` only — **zero external dependencies**
> (WebSocket bundles its own SHA-1 + Base64; ICMP ping is implemented natively)
> Reference architecture: BusyBox

---

## Features

- **Multi-protocol**: TCP / UDP / WebSocket (RFC 6455 text frames)
- **Dual role**: every protocol can run as **client** or **server**
- **Batch ping** with rich input: single IP, last-octet range
  (`192.168.1.10-50`), full IP range (`a-b`), CIDR (`10.0.0.0/24`),
  or a hosts file. Both TCP-ping and **native ICMP ping** (raw socket,
  no shelling out to `/bin/ping`).
- **Reverse DNS** column: ping results show the resolved hostname for each IP
- **Batch protocol test**: `btest` runs concurrent TCP / UDP / WS probes against
  many `host:port[:proto]` targets
- **Bilingual UI**: compile-time `make LANG=zh|en`; runtime `--lang zh|en`
  or `ST_LANG` env var
- **Pretty CLI**: Unicode icons (✔ ✘ ⚠ ℹ ◀ ▶ ⏱ 🚀 🌐), bright color palette,
  CJK-width-aware tables, ASCII fallback on non-UTF-8 terminals
- **Bash tab completion** for all sub-commands, options, and enum values
- **Test suite**: `make test` runs both C unit tests and end-to-end shell tests

---

## Layout

```
socketTool/
├── Makefile
├── README.md           # this file (English)
├── README.zh.md        # Chinese
├── src/
│   ├── core/           # entry point + applet dispatch
│   ├── ui/             # colors, icons, tables, progress
│   ├── i18n/           # bilingual string tables
│   ├── net/            # socket helpers, ICMP, IP range/CIDR expansion
│   └── applets/        # tcp.c udp.c ws.c bping.c btest.c
├── tests/              # unit + e2e tests
├── scripts/            # bash-completion script
└── examples/           # sample hosts / targets files
```

---

## Build

```bash
make help                  # list all targets
make                       # build (English UI by default)
make LANG=zh               # build with Chinese UI as default
make links                 # create applet symlinks in cwd
make install PREFIX=/usr/local
make test                  # run the full test suite
make clean
```

`make install` also deploys the bash-completion script to
`$PREFIX/share/bash-completion/completions/socketTool`.

---

## Usage

```bash
# Main binary + sub-command
socketTool <applet> [options]

# Or directly via the applet symlink
tcp-client -H 192.168.1.10 -p 8080
```

Run `socketTool` with no arguments to see the colored applet list, or
`socketTool <applet> -h` for per-applet help.

### Global flags (accepted before or after the applet)

| Flag                | Description                          |
| ------------------- | ------------------------------------ |
| `--lang en\|zh`     | Switch UI language at runtime        |
| `--no-color`        | Disable colors (also `NO_COLOR=1`)   |
| `-V, --version`     | Print version                        |

### Applets

| Applet        | Description                                              |
| ------------- | -------------------------------------------------------- |
| `tcp-client`  | TCP client: connect / send / interactive / count         |
| `tcp-server`  | TCP server: listen / echo \| discard                     |
| `udp-client`  | UDP client: multi-send + RTT / loss stats                |
| `udp-server`  | UDP server: echo \| discard                              |
| `ws-client`   | WebSocket client (ws://)                                 |
| `ws-server`   | WebSocket server                                         |
| `bping`       | Batch ping (single / range / CIDR / file, ICMP \| TCP)   |
| `btest`       | Batch protocol connectivity test (TCP / UDP / WS)        |

### Examples

#### TCP

```bash
socketTool tcp-server -p 9000
socketTool tcp-client -H 192.168.1.10 -p 9000 -m "hello"
socketTool tcp-client -H 192.168.1.10 -p 9000 -i        # interactive
```

#### UDP

```bash
socketTool udp-server -p 9001
socketTool udp-client -H 192.168.1.10 -p 9001 -m ping -c 5 -i 200 -w 500
```

#### WebSocket

```bash
socketTool ws-server -p 9002
socketTool ws-client -H 192.168.1.10 -p 9002 -m '{"hello":"ws"}'
```

#### Batch ping (range / CIDR, native ICMP)

```bash
# Native ICMP ping (raw socket; needs CAP_NET_RAW or root,
# or set the unprivileged ICMP sysctl: see "Permissions" below)
socketTool bping -m icmp 8.8.8.8 1.1.1.1

# TCP-ping (no privileges required)
socketTool bping -m tcp -p 22 192.168.1.0/24

# Mix CLI + file, custom concurrency
socketTool bping -p 22 -j 64 -t 800 \
    -f examples/hosts.txt 192.168.1.10-50 router.local
```

#### Batch protocol test

```bash
socketTool btest 192.168.1.10:80:tcp 192.168.1.10:53:udp 192.168.1.10:8080:ws
socketTool btest -f examples/targets.txt -P tcp -j 32
```

---

## Permissions for native ICMP

Native ICMP echo uses raw sockets and normally requires either:

- running as root, or
- `setcap cap_net_raw+ep ./socketTool`, or
- enabling unprivileged ICMP for your group:
  `sudo sysctl -w net.ipv4.ping_group_range="0 2147483647"`
  (Linux supports `IPPROTO_ICMP` datagram sockets for non-root since 3.0;
  socketTool falls back to that automatically when `SOCK_RAW` is denied.)

If neither is available, use `-m tcp` (the default), which needs no privileges.

---

## Bilingual UI

```bash
# Compile-time default
make LANG=zh

# Runtime override (takes precedence over compile default)
socketTool --lang zh bping 10.0.0.0/30
ST_LANG=zh tcp-client -H 127.0.0.1 -p 9000
```

---

## Tab completion

```bash
# Try without installing
. scripts/socketTool.bash-completion
```

After `make install`, the script is auto-loaded if your distro enables
bash-completion. Completions provided:

- `socketTool <Tab>` → list applets
- `tcp-client -<Tab>` → list options
- `bping -m <Tab>` → `tcp icmp`
- `btest -P <Tab>` → `tcp udp ws`
- `--lang <Tab>` → `en zh`
- `-H <Tab>` → `/etc/hosts` + `localhost / 127.0.0.1`
- `-f <Tab>` → file names

---

## Tests

```bash
make test
```

Coverage:

- `tests/unit_range.c` — unit tests for `host_range_expand`
- `tests/test_dispatch.sh` — main binary, `--lang`, symlink dispatch
- `tests/test_tcp.sh` `test_udp.sh` `test_ws.sh` — protocol e2e
- `tests/test_bping.sh` `test_btest.sh` — batch applets

---

## Exit codes

| Code | Meaning                                  |
| ---- | ---------------------------------------- |
| 0    | success                                  |
| 1    | bad arguments / usage                    |
| 2    | resource init failed (connect / listen)  |
| 3    | I/O error                                |
| 4    | batch job had at least one failure       |

---

## License

TBD
