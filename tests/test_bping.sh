#!/usr/bin/env bash
# tests/test_bping.sh — batch ping with range/CIDR
. "$(dirname "$0")/lib.sh"

section "bping (range / CIDR)"

PORT=$(free_port)
spawn "$BIN" tcp-server -p "$PORT" -e >/dev/null
wait_port "$PORT" || { echo "tcp server didn't start"; exit 1; }

tcheck "bping single host succeeds" \
    "'$BIN' bping -p $PORT -t 500 -j 4 127.0.0.1 | grep -qE 'OK'"

tcheck "bping last-octet range parses" \
    "'$BIN' bping -p $PORT -t 200 -j 8 127.0.0.1-3 | grep -q '127.0.0.3'"

tcheck "bping CIDR /30 expands to 2 hosts" \
    "'$BIN' bping -p $PORT -t 200 -j 8 127.0.0.0/30 \
     | grep -E 'hosts' | grep -q '2'"

tcheck "bping reports failure on closed port" \
    "out=\$('$BIN' bping -p 1 -t 200 127.0.0.1 || true); \
     echo \"\$out\" | grep -q FAIL"
