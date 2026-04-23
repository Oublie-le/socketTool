#!/usr/bin/env bash
# tests/test_ws.sh — websocket client + server
. "$(dirname "$0")/lib.sh"

section "websocket client/server"

PORT=$(free_port)
spawn "$BIN" ws-server -p "$PORT" -e -1 >/dev/null
wait_port "$PORT" || { echo "ws server didn't start"; exit 1; }

tcheck "ws handshake completes & echo received" \
    "out=\$('$BIN' ws-client -H 127.0.0.1 -p $PORT -m hello-ws -t 1000); \
     echo \"\$out\" | grep -q hello-ws"
