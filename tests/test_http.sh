#!/usr/bin/env bash
# tests/test_http.sh — http-client + http-server end-to-end
. "$(dirname "$0")/lib.sh"

section "http client/server"

PORT=$(free_port)
spawn "$BIN" http-server -p "$PORT" >/dev/null
wait_port "$PORT" || { echo "http server didn't start"; exit 1; }

tcheck "http GET / returns 200" \
    "out=\$('$BIN' http-client -t 1000 http://127.0.0.1:$PORT/ 2>&1); \
     echo \"\$out\" | grep -q 'status.*200'"

tcheck "http POST sets method + body" \
    "out=\$('$BIN' http-client -X POST -d hello -t 1000 http://127.0.0.1:$PORT/ 2>&1); \
     echo \"\$out\" | grep -q 'status.*200'"

# static-root mode
ROOT=$(mktemp -d)
echo "<h1>ok</h1>" > "$ROOT/index.html"
PORT2=$(free_port)
spawn "$BIN" http-server -p "$PORT2" -r "$ROOT" >/dev/null
wait_port "$PORT2" || { echo "http static didn't start"; exit 1; }

OUT=$(mktemp)
tcheck "http static serves index.html" \
    "'$BIN' http-client -o $OUT -t 1000 http://127.0.0.1:$PORT2/ >/dev/null 2>&1; \
     grep -q 'ok' $OUT"
rm -rf "$ROOT" "$OUT"

tcheck "http 404 for missing file (static)" \
    "out=\$('$BIN' http-client -t 1000 http://127.0.0.1:$PORT2/no-such 2>&1); \
     echo \"\$out\" | grep -q 'status.*404'"
