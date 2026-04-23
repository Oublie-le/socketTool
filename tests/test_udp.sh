#!/usr/bin/env bash
# tests/test_udp.sh — udp-client + udp-server
. "$(dirname "$0")/lib.sh"

section "udp client/server"

PORT=$(free_port)
spawn "$BIN" udp-server -p "$PORT" -e >/dev/null
sleep 0.2

tcheck "udp client gets echo" \
    "out=\$('$BIN' udp-client -H 127.0.0.1 -p $PORT -m hello-udp -c 2 -i 50 -w 300); \
     echo \"\$out\" | grep -q hello-udp"

tcheck "udp client summary printed" \
    "'$BIN' --lang en udp-client -H 127.0.0.1 -p $PORT -m x -c 1 -i 0 -w 200 | grep -qi sent"
