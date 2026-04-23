#!/usr/bin/env bash
# tests/test_dispatch.sh — main binary + symlink dispatch
. "$(dirname "$0")/lib.sh"

section "dispatch & global flags"

# main help (returns 0 with explicit help, 1 with no args)
tcheck "socketTool --help exits 0"        "'$BIN' --help >/dev/null"
tcheck "socketTool prints applet list"    "'$BIN' --help | grep -q tcp-client"
tcheck "unknown applet exits non-zero"    "! '$BIN' nope-applet >/dev/null 2>&1"
tcheck "version flag works"               "'$BIN' --version | grep -q socketTool"

# i18n switch
tcheck "english title shown by --lang en" "'$BIN' --lang en --help | grep -q 'multi-protocol'"
tcheck "chinese title shown by --lang zh" "'$BIN' --lang zh --help | grep -q '多协议'"
tcheck "ST_LANG env switches output"      "ST_LANG=zh '$BIN' --help | grep -q '可用子命令'"

# symlink dispatch: tcp-client -h must show its own usage
tcheck "symlink tcp-client --help works"  "'$ROOT/tcp-client' --lang en --help | grep -qi usage"
