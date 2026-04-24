#!/usr/bin/env bash
# tests/test_mqtt.sh — mqtt-client + mqtt-server (broker) end-to-end
. "$(dirname "$0")/lib.sh"

section "mqtt client/broker"

PORT=$(free_port)
spawn "$BIN" mqtt-server -p "$PORT" >/dev/null
wait_port "$PORT" || { echo "mqtt broker didn't start"; exit 1; }

# 1) plain publish-only (CONNACK + PUBLISH, no subscribers)
tcheck "mqtt publish completes" \
    "'$BIN' mqtt-client -H 127.0.0.1 -p $PORT -t st/test -m hello-mqtt -k 5 \
        2>&1 | grep -q 'published'"

# 2) sub + pub roundtrip via the broker
SUB_OUT=$(mktemp)
"$BIN" mqtt-client -H 127.0.0.1 -p $PORT -t st/loop -s -c 1 -k 5 \
    >"$SUB_OUT" 2>&1 &
subpid=$!
sleep 0.3
"$BIN" mqtt-client -H 127.0.0.1 -p $PORT -t st/loop -m hi-loop -k 5 >/dev/null 2>&1
wait "$subpid" 2>/dev/null
tcheck "mqtt subscriber receives broker-relayed publish" \
    "grep -q 'hi-loop' $SUB_OUT"
rm -f "$SUB_OUT"

# 3) wildcard topic
WC_OUT=$(mktemp)
"$BIN" mqtt-client -H 127.0.0.1 -p $PORT -t 'st/+/x' -s -c 1 -k 5 \
    >"$WC_OUT" 2>&1 &
subpid=$!
sleep 0.3
"$BIN" mqtt-client -H 127.0.0.1 -p $PORT -t 'st/abc/x' -m wc-msg -k 5 >/dev/null 2>&1
wait "$subpid" 2>/dev/null
tcheck "mqtt + wildcard delivers" \
    "grep -q 'wc-msg' $WC_OUT"
rm -f "$WC_OUT"
