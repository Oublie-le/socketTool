#!/usr/bin/env bash
# tests/test_stress.sh — stress / load tests for tcp / udp / ws / bping.
#
# Bounded so the suite still finishes quickly in CI:
#   - TCP: 50 concurrent clients
#   - UDP: 200 sequential datagrams
#   - WS:  20 sequential clients
#   - bping: scan localhost x100 (loopback IPs)
#   - bench: 1-second run, must yield > 1 Mbps
. "$(dirname "$0")/lib.sh"

section "stress / load"

# ---- TCP: 50 concurrent clients all get their echo back -------------------
PORT=$(free_port)
spawn "$BIN" tcp-server -p "$PORT" -e >/dev/null
wait_port "$PORT" || { echo "tcp server didn't start"; exit 1; }

tmp=$(mktemp -d)
N=50
pids=()
for i in $(seq 1 $N); do
    (
        out=$("$BIN" tcp-client -H 127.0.0.1 -p "$PORT" -m "msg-$i" -t 2000 2>&1)
        echo "$out" | grep -q "msg-$i" && echo OK > "$tmp/$i" || echo FAIL > "$tmp/$i"
    ) &
    pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
ok=$(grep -l OK "$tmp"/* 2>/dev/null | wc -l)
rm -rf "$tmp"
tcheck "tcp: $N concurrent clients all echoed" \
    "[ $ok -eq $N ]"

# ---- TCP bench: must report a non-zero Mbps reading -----------------------
PORT2=$(free_port)
spawn "$BIN" tcp-server -p "$PORT2" -d >/dev/null
wait_port "$PORT2" || { echo "tcp bench server didn't start"; exit 1; }

tcheck "tcp bench reports throughput > 1 Mbps" \
    "out=\$('$BIN' tcp-client -H 127.0.0.1 -p $PORT2 -B 1 2>&1); \
     mbps=\$(echo \"\$out\" | grep -oE '[0-9]+\\.[0-9]+ Mbps' | head -1 | awk '{print int(\$1)}'); \
     [ \"\${mbps:-0}\" -ge 1 ]"

# ---- UDP: 200 datagrams round-trip ----------------------------------------
PORT3=$(free_port)
spawn "$BIN" udp-server -p "$PORT3" -e >/dev/null
sleep 0.2

tcheck "udp: 200 sequential echoes succeed" \
    "out=\$('$BIN' --lang en udp-client -H 127.0.0.1 -p $PORT3 -m ping -c 200 -i 0 -w 300 2>&1); \
     lost=\$(echo \"\$out\" | grep -oE 'lost[^0-9]*[0-9]+' | grep -oE '[0-9]+\$' | head -1); \
     [ \"\${lost:-99}\" -eq 0 ]"

# ---- UDP bench ------------------------------------------------------------
PORT4=$(free_port)
spawn "$BIN" udp-server -p "$PORT4" -d >/dev/null
sleep 0.2

tcheck "udp bench reports throughput > 1 Mbps" \
    "out=\$('$BIN' udp-client -H 127.0.0.1 -p $PORT4 -B 1 2>&1); \
     mbps=\$(echo \"\$out\" | grep -oE '[0-9]+\\.[0-9]+ Mbps' | head -1 | awk '{print int(\$1)}'); \
     [ \"\${mbps:-0}\" -ge 1 ]"

# ---- WS: 20 sequential clients --------------------------------------------
PORT5=$(free_port)
spawn "$BIN" ws-server -p "$PORT5" -e >/dev/null
wait_port "$PORT5" || { echo "ws server didn't start"; exit 1; }

ws_ok=0
for i in $(seq 1 20); do
    out=$("$BIN" ws-client -H 127.0.0.1 -p "$PORT5" -m "ws-$i" -t 1000 2>&1)
    echo "$out" | grep -q "ws-$i" && ws_ok=$((ws_ok+1))
done
tcheck "ws: 20 sequential clients all echoed" \
    "[ $ws_ok -eq 20 ]"

# ---- bping: scan 100 loopback hosts in parallel ---------------------------
# 127.0.0.1-100 — all loopback, all reachable.
tcheck "bping: 100 parallel TCP-pings to loopback finish without crash" \
    "'$BIN' bping -m tcp -p 22 -j 32 -t 200 -D -n -o csv 127.0.0.1-100 \
        2>&1 | wc -l | awk '{exit (\$1 < 50)}'"

# ---- bping: --watch runs at least 2 rounds then we kill it ----------------
PORT6=$(free_port)
spawn "$BIN" tcp-server -p "$PORT6" -d >/dev/null
wait_port "$PORT6" || { echo "watch tcp server didn't start"; exit 1; }
log=$(mktemp)
"$BIN" bping -m tcp -p "$PORT6" -t 200 -D -n -W 1 127.0.0.1 >"$log" 2>&1 &
wpid=$!
sleep 2.5
kill "$wpid" 2>/dev/null; wait "$wpid" 2>/dev/null
rounds=$(grep -c "127.0.0.1" "$log")
rm -f "$log"
tcheck "bping --watch produced >= 2 rounds in 2.5s" \
    "[ ${rounds:-0} -ge 2 ]"
