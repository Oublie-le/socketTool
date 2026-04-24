#!/usr/bin/env bash
# tests/test_stress.sh — stress / load tests for tcp / udp / ws / bping.
#
# All workload sizes are configurable via env vars (defaults keep CI fast):
#
#   STRESS_TCP_CONC     concurrent tcp-client count           (default 50)
#   STRESS_UDP_PKTS     sequential udp datagrams per client   (default 200)
#   STRESS_WS_SEQ       sequential ws-client count            (default 20)
#   STRESS_BPING_HOSTS  bping loopback host count (1-N)       (default 100)
#   STRESS_BENCH_SECS   tcp/udp -B benchmark duration (s)     (default 1)
#   STRESS_BENCH_MIN    minimum acceptable throughput (Mbps)  (default 1)
#   STRESS_WATCH_SECS   bping --watch wall-clock (s)          (default 2.5)
#   STRESS_WATCH_MIN    minimum --watch rounds expected       (default 2)
#
# Examples:
#   STRESS_TCP_CONC=500 STRESS_UDP_PKTS=2000 bash tests/test_stress.sh
#   make test STRESS_BENCH_SECS=5
. "$(dirname "$0")/lib.sh"

TCP_CONC=${STRESS_TCP_CONC:-50}
UDP_PKTS=${STRESS_UDP_PKTS:-200}
WS_SEQ=${STRESS_WS_SEQ:-20}
BPING_HOSTS=${STRESS_BPING_HOSTS:-100}
BENCH_SECS=${STRESS_BENCH_SECS:-1}
BENCH_MIN=${STRESS_BENCH_MIN:-1}
WATCH_SECS=${STRESS_WATCH_SECS:-2.5}
WATCH_MIN=${STRESS_WATCH_MIN:-2}

section "stress / load  (tcp=$TCP_CONC udp=$UDP_PKTS ws=$WS_SEQ bping=$BPING_HOSTS bench=${BENCH_SECS}s)"

# ---- TCP: N concurrent clients all get their echo back --------------------
PORT=$(free_port)
spawn "$BIN" tcp-server -p "$PORT" -e >/dev/null
wait_port "$PORT" || { echo "tcp server didn't start"; exit 1; }

tmp=$(mktemp -d)
pids=()
for i in $(seq 1 "$TCP_CONC"); do
    (
        out=$("$BIN" tcp-client -H 127.0.0.1 -p "$PORT" -m "msg-$i" -t 2000 2>&1)
        echo "$out" | grep -q "msg-$i" && echo OK > "$tmp/$i" || echo FAIL > "$tmp/$i"
    ) &
    pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
ok=$(grep -l OK "$tmp"/* 2>/dev/null | wc -l)
rm -rf "$tmp"
tcheck "tcp: $TCP_CONC concurrent clients all echoed" \
    "[ $ok -eq $TCP_CONC ]"

# ---- TCP bench: throughput >= BENCH_MIN Mbps ------------------------------
PORT2=$(free_port)
spawn "$BIN" tcp-server -p "$PORT2" -d >/dev/null
wait_port "$PORT2" || { echo "tcp bench server didn't start"; exit 1; }

tcheck "tcp bench (${BENCH_SECS}s) >= ${BENCH_MIN} Mbps" \
    "out=\$('$BIN' tcp-client -H 127.0.0.1 -p $PORT2 -B $BENCH_SECS 2>&1); \
     mbps=\$(echo \"\$out\" | grep -oE '[0-9]+\\.[0-9]+ Mbps' | head -1 | awk '{print int(\$1)}'); \
     [ \"\${mbps:-0}\" -ge $BENCH_MIN ]"

# ---- UDP: N datagrams round-trip ------------------------------------------
PORT3=$(free_port)
spawn "$BIN" udp-server -p "$PORT3" -e >/dev/null
sleep 0.2

tcheck "udp: $UDP_PKTS sequential echoes succeed" \
    "out=\$('$BIN' --lang en udp-client -H 127.0.0.1 -p $PORT3 -m ping -c $UDP_PKTS -i 0 -w 300 2>&1); \
     lost=\$(echo \"\$out\" | grep -oE 'lost[^0-9]*[0-9]+' | grep -oE '[0-9]+\$' | head -1); \
     [ \"\${lost:-99}\" -eq 0 ]"

# ---- UDP bench ------------------------------------------------------------
PORT4=$(free_port)
spawn "$BIN" udp-server -p "$PORT4" -d >/dev/null
sleep 0.2

tcheck "udp bench (${BENCH_SECS}s) >= ${BENCH_MIN} Mbps" \
    "out=\$('$BIN' udp-client -H 127.0.0.1 -p $PORT4 -B $BENCH_SECS 2>&1); \
     mbps=\$(echo \"\$out\" | grep -oE '[0-9]+\\.[0-9]+ Mbps' | head -1 | awk '{print int(\$1)}'); \
     [ \"\${mbps:-0}\" -ge $BENCH_MIN ]"

# ---- WS: N sequential clients ---------------------------------------------
PORT5=$(free_port)
spawn "$BIN" ws-server -p "$PORT5" -e >/dev/null
wait_port "$PORT5" || { echo "ws server didn't start"; exit 1; }

ws_ok=0
for i in $(seq 1 "$WS_SEQ"); do
    out=$("$BIN" ws-client -H 127.0.0.1 -p "$PORT5" -m "ws-$i" -t 1000 2>&1)
    echo "$out" | grep -q "ws-$i" && ws_ok=$((ws_ok+1))
done
tcheck "ws: $WS_SEQ sequential clients all echoed" \
    "[ $ws_ok -eq $WS_SEQ ]"

# ---- bping: scan N loopback hosts in parallel -----------------------------
tcheck "bping: $BPING_HOSTS parallel TCP-pings to loopback finish without crash" \
    "'$BIN' bping -m tcp -p 22 -j 32 -t 200 -D -n -o csv 127.0.0.1-$BPING_HOSTS \
        2>&1 | wc -l | awk -v want=$((BPING_HOSTS / 2)) '{exit (\$1 < want)}'"

# ---- bping: --watch runs at least WATCH_MIN rounds -----------------------
PORT6=$(free_port)
spawn "$BIN" tcp-server -p "$PORT6" -d >/dev/null
wait_port "$PORT6" || { echo "watch tcp server didn't start"; exit 1; }
log=$(mktemp)
"$BIN" bping -m tcp -p "$PORT6" -t 200 -D -n -W 1 127.0.0.1 >"$log" 2>&1 &
wpid=$!
sleep "$WATCH_SECS"
kill "$wpid" 2>/dev/null; wait "$wpid" 2>/dev/null
rounds=$(grep -c "127.0.0.1" "$log")
rm -f "$log"
tcheck "bping --watch produced >= $WATCH_MIN rounds in ${WATCH_SECS}s" \
    "[ ${rounds:-0} -ge $WATCH_MIN ]"
