#!/usr/bin/env bash
# tests/test_btest.sh — batch protocol connectivity test
. "$(dirname "$0")/lib.sh"

section "btest (multi-protocol)"

TPORT=$(free_port)
UPORT=$(free_port)
WPORT=$(free_port)
spawn "$BIN" tcp-server -p "$TPORT" -e    >/dev/null
spawn "$BIN" udp-server -p "$UPORT" -e    >/dev/null
spawn "$BIN" ws-server  -p "$WPORT" -e    >/dev/null
wait_port "$TPORT" || { echo "tcp didn't start"; exit 1; }
wait_port "$WPORT" || { echo "ws didn't start"; exit 1; }
sleep 0.2

tcheck "btest tcp target OK" \
    "'$BIN' btest -t 300 127.0.0.1:$TPORT:tcp | grep -q OK"

tcheck "btest ws target reports 101 upgrade" \
    "'$BIN' btest -t 500 127.0.0.1:$WPORT:ws | grep -q '101 upgrade'"

tcheck "btest udp target reachable" \
    "'$BIN' btest -t 300 127.0.0.1:$UPORT:udp | grep -q OK"

tcheck "btest reports FAIL for closed tcp port" \
    "out=\$('$BIN' btest -t 200 127.0.0.1:1:tcp || true); \
     echo \"\$out\" | grep -q FAIL"
