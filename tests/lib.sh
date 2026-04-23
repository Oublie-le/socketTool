#!/usr/bin/env bash
# tests/lib.sh — common helpers for the test harness.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/socketTool"

# colors (only in TTY)
if [ -t 1 ]; then
    C_OK=$'\e[92m'; C_FAIL=$'\e[91m'; C_DIM=$'\e[2m'; C_RST=$'\e[0m'
    C_HEAD=$'\e[1;96m'
else
    C_OK=''; C_FAIL=''; C_DIM=''; C_RST=''; C_HEAD=''
fi

PASS=0
FAIL=0
FAILED_NAMES=()

# pick a likely-free port in 20000-29999
free_port() {
    local p
    while :; do
        p=$(( 20000 + RANDOM % 10000 ))
        (echo > /dev/tcp/127.0.0.1/$p) >/dev/null 2>&1 || { echo $p; return; }
    done
}

# wait until a port is listening (up to 2s)
wait_port() {
    local port=$1 i
    for i in $(seq 1 40); do
        (echo > /dev/tcp/127.0.0.1/$port) >/dev/null 2>&1 && return 0
        sleep 0.05
    done
    return 1
}

# tcase NAME COMMAND...
tcase() {
    local name=$1; shift
    local out rc
    out=$("$@" 2>&1) ; rc=$?
    if [ $rc -eq 0 ]; then
        printf "  ${C_OK}✔${C_RST} %s\n" "$name"
        PASS=$((PASS+1))
    else
        printf "  ${C_FAIL}✘${C_RST} %s ${C_DIM}(rc=$rc)${C_RST}\n" "$name"
        FAIL=$((FAIL+1))
        FAILED_NAMES+=("$name")
        echo "$out" | sed 's/^/      /'
    fi
}

# tassert NAME [success_msg]   — pipe the boolean test in, this records pass/fail
# Used like: tassert "name" <<< "$([ x = x ] && echo ok)"
# Simpler approach below.

# tcheck NAME EXPR_STRING  — runs `bash -c EXPR_STRING` and records
tcheck() {
    local name=$1 expr=$2
    if bash -c "$expr" >/dev/null 2>&1; then
        printf "  ${C_OK}✔${C_RST} %s\n" "$name"
        PASS=$((PASS+1))
    else
        printf "  ${C_FAIL}✘${C_RST} %s\n" "$name"
        FAIL=$((FAIL+1))
        FAILED_NAMES+=("$name")
    fi
}

section() { printf "\n${C_HEAD}▶ %s${C_RST}\n" "$1"; }

summary() {
    echo
    echo "─────────────────────────────────────────────"
    if [ $FAIL -eq 0 ]; then
        printf "${C_OK}✔ all tests passed${C_RST}  (%d)\n" "$PASS"
    else
        printf "${C_FAIL}✘ %d failed${C_RST} / %d passed\n" "$FAIL" "$PASS"
        for n in "${FAILED_NAMES[@]}"; do echo "    - $n"; done
    fi
    echo "─────────────────────────────────────────────"
    return $FAIL
}


cleanup_pids=()
on_exit() {
    summary; local rc=$?
    for p in "${cleanup_pids[@]:-}"; do kill "$p" 2>/dev/null; done
    wait 2>/dev/null
    exit $rc
}
trap on_exit EXIT

# spawn server in background, push pid to cleanup
spawn() {
    "$@" >/dev/null 2>&1 &
    cleanup_pids+=($!)
    echo $!
}
