#!/usr/bin/env bash
# tests/run_all.sh — run the full test suite.
#
# Builds the unit test, then runs each test_*.sh script.
# Exits non-zero if any sub-script reports a failure.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [ -t 1 ]; then
    HEAD=$'\e[1;95m'; OK=$'\e[92m'; FAIL=$'\e[91m'; RST=$'\e[0m'
else
    HEAD=''; OK=''; FAIL=''; RST=''
fi

# 1. ensure binary + symlinks present
if [ ! -x "$ROOT/socketTool" ]; then
    echo "${FAIL}✘ socketTool binary not built. Run 'make' first.${RST}" >&2
    exit 2
fi

# 2. compile unit test against net.c (re-link with -Isrc)
printf "${HEAD}━━━ unit tests ━━━${RST}\n"
UNIT_BIN=$(mktemp)
trap 'rm -f "$UNIT_BIN"' EXIT
if ! gcc -O0 -g -Wall -Isrc -o "$UNIT_BIN" \
        tests/unit_range.c src/net/net.c -lpthread 2>&1; then
    echo "${FAIL}✘ failed to build unit_range${RST}" >&2
    exit 2
fi
"$UNIT_BIN"
UNIT_RC=$?

# 3. run shell tests
TOTAL_FAIL=$UNIT_RC
for t in tests/test_*.sh; do
    printf "\n${HEAD}━━━ %s ━━━${RST}\n" "$(basename "$t")"
    bash "$t"; rc=$?
    if [ $rc -ne 0 ]; then TOTAL_FAIL=$((TOTAL_FAIL + 1)); fi
done

echo
if [ $TOTAL_FAIL -eq 0 ]; then
    printf "${OK}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
    printf "  ALL TESTS PASSED${RST}\n"
    printf "${OK}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RST}\n"
    exit 0
else
    printf "${FAIL}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
    printf "  %d TEST GROUP(S) FAILED${RST}\n" "$TOTAL_FAIL"
    printf "${FAIL}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RST}\n"
    exit 1
fi
