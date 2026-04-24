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

- **Multi-protocol**: TCP / UDP / WebSocket (RFC 6455 text frames) / HTTP/1.1 / MQTT 3.1.1 (QoS 0)
- **Dual role**: every protocol can run as **client** or **server**
  (TCP / UDP / WS / HTTP / MQTT all ship with both sides; servers spawn one
  thread per connection so multiple clients no longer block each other)
- **Batch ping** with rich input: single IP, last-octet range
  (`192.168.1.10-50`), full IP range (`a-b`), CIDR (`10.0.0.0/24`),
  or a hosts file. Both TCP-ping and **native ICMP ping** (raw socket,
  no shelling out to `/bin/ping`).
- **Reverse DNS** column: ping results show the resolved hostname for each IP
- **Batch protocol test**: `btest` runs concurrent TCP / UDP / WS probes against
  many `host:port[:proto]` targets
- **Throughput bench** (`-B SECS` on tcp/udp clients): iperf-style one-shot
  Mbps measurement against a server in `discard` mode (`-d`)
- **LAN discovery for bping**: each host row resolves to rDNS / mDNS / NetBIOS
  hostname + MAC (from ARP), with columns `ip / hostname / src / mac / rtt`
- **Output formats** (bping): `-o table|json|csv` plus `--watch SECS` for
  continuous re-scan dashboards
- **Local self-check**: `diag` reports interfaces, default route, resolvers,
  MTU and runs a few TCP reachability probes (gateway / 1.1.1.1 / 8.8.8.8)
- **Host aliases**: `~/.socketToolrc` maps `@name` → `host[:port]` (e.g.
  `router 192.168.1.1`, `db=db.internal:5432`), usable on every `-H`
- **Bilingual UI**: compile-time `make UILANG=zh|en`; runtime `--lang zh|en`
  or `ST_LANG` env var
- **Pretty CLI**: Unicode icons (✔ ✘ ⚠ ℹ ◀ ▶ ⏱ 🚀 🌐), bright color palette,
  CJK-width-aware tables, ASCII fallback on non-UTF-8 terminals
- **Bash tab completion** for all sub-commands, options, and enum values
- **Quality targets**: `make lint` (cppcheck), `make coverage` (gcov/lcov HTML),
  plus GitHub Actions CI (EN/ZH build + full test matrix)
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
make UILANG=zh               # build with Chinese UI as default
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
| `diag`        | Local network self-check (interfaces / gateway / DNS / MTU) |
| `http-client` | HTTP/1.1 client: GET / POST, custom headers, follow 3xx  |
| `http-server` | HTTP/1.1 server: canned response or static `--root` directory |
| `mqtt-client` | MQTT 3.1.1 client (publish / subscribe, QoS 0)           |
| `mqtt-server` | Minimal MQTT 3.1.1 broker (multi-client, QoS 0, `+` and `#` wildcards) |

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

#### Throughput bench (iperf-style)

```bash
# server in discard mode (low-noise)
socketTool tcp-server -p 9000 -d &
socketTool tcp-client -H 127.0.0.1 -p 9000 -B 5           # 5-second bench

# UDP equivalent (1400-byte MTU-safe datagrams)
socketTool udp-server -p 9001 -d &
socketTool udp-client -H 127.0.0.1 -p 9001 -B 5
```

#### Local network self-check

```bash
socketTool diag
# reports: interfaces / default route / resolvers / MTU / a few TCP probes
```

#### Host aliases (~/.socketToolrc)

```bash
cat > ~/.socketToolrc <<EOF
# name  host[:port]
router  192.168.1.1
db=db.internal:5432
EOF
socketTool tcp-client -H @router -p 80
socketTool tcp-client -H @db                # port picked from alias
```

#### bping — JSON / CSV / watch

```bash
socketTool bping -o json 192.168.1.0/24      > hosts.json
socketTool bping -o csv  -f examples/hosts.txt > hosts.csv
socketTool bping -W 5    192.168.1.0/24      # re-scan every 5s (Ctrl-C)
```

#### HTTP

```bash
socketTool http-server -p 8080 -r ./public       # static site
socketTool http-client http://127.0.0.1:8080/
socketTool http-client -X POST -d '{"k":"v"}' \
    -H 'Content-Type: application/json' http://api.example.com/data
socketTool http-client -L http://example.com/   # follow redirects
```

#### MQTT (3.1.1, QoS 0)

```bash
# broker on 1883
socketTool mqtt-server -p 1883 &

# subscribe, print 10 messages then exit
socketTool mqtt-client -H 127.0.0.1 -p 1883 -t 'sensors/+/temp' -s -c 10

# publish once
socketTool mqtt-client -H 127.0.0.1 -p 1883 -t 'sensors/a/temp' -m '23.5'
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
make UILANG=zh

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
