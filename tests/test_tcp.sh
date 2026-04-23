#!/usr/bin/env bash
# tests/test_tcp.sh — tcp-client + tcp-server end-to-end
. "$(dirname "$0")/lib.sh"

section "tcp client/server"

PORT=$(free_port)
spawn "$BIN" tcp-server -p "$PORT" -e >/dev/null
wait_port "$PORT" || { echo "server didn't start"; exit 1; }

tcheck "tcp client connects & echoes" \
    "out=\$('$BIN' tcp-client -H 127.0.0.1 -p $PORT -m hello-tcp -t 1000); \
     echo \"\$out\" | grep -q hello-tcp"

tcheck "tcp client to closed port fails" \
    "! '$BIN' tcp-client -H 127.0.0.1 -p 1 -m x -t 200 >/dev/null 2>&1"
