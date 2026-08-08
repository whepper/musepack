#!/usr/bin/env bash
# Phase 4 integration tests for musicpack-server: builds a small library from
# the reference fixtures, scans it, serves it, and exercises the HTTP API v1
# (JSON endpoints, collector hierarchy, HTTP Range streaming, HEAD, errors,
# concurrency and security boundaries) via server_api_test.py.
#
# Usage: tests/run_server.sh <musicpack-server-bin>
set -u

SERVER="${1:?musicpack-server binary}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REF="$ROOT/tests/reference"
PY="$(command -v python3 || command -v python)"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mpack-server.XXXXXX")"
PORT="$("$PY" -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')"

cleanup() {
    [ -n "${PID:-}" ] && kill "$PID" 2>/dev/null
    wait "${PID:-}" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

# 1. build the fixture library
"$PY" "$ROOT/tests/server_api_test.py" setup \
    "$REF/test-musicpack-album.mpack" \
    "$REF/test-flac-album.mpack" \
    "$TMP" || { echo "FAIL setup"; exit 1; }
LIB="$(cat "$TMP/libdir")"

# 2. scan
"$SERVER" scan --library "$LIB" --database "$TMP/lib.db" >/dev/null 2>&1 || {
    echo "FAIL scan"; exit 1; }

# 2b. create an API token for the test client
TOKEN="$("$SERVER" token create --name "CI" --database "$TMP/lib.db" 2>/dev/null \
    | grep '^mpk_')"
[ -n "$TOKEN" ] || { echo "FAIL token create"; exit 1; }

# 3. serve (no startup scan; scan already done). --static-dir serves the
#    reference demo (with COOP/COEP), --allow-origin exercises CORS.
"$SERVER" serve --library "$LIB" --database "$TMP/lib.db" \
    --listen 127.0.0.1 --port "$PORT" --no-scan \
    --static-dir "$ROOT/demo" \
    --allow-origin http://localhost:5173 \
    >"$TMP/serve.log" 2>&1 &
PID=$!

# 4. wait for readiness
READY=0
for _ in $(seq 1 50); do
    if curl -s "http://127.0.0.1:$PORT/api/v1/health" >/dev/null 2>&1; then
        READY=1
        break
    fi
    sleep 0.1
done
if [ "$READY" != 1 ]; then
    echo "FAIL server did not become ready"; cat "$TMP/serve.log"; exit 1
fi

# 5. run the API/streaming/security tests
"$PY" "$ROOT/tests/server_api_test.py" run \
    "http://127.0.0.1:$PORT" "$LIB" "$TOKEN" "$ROOT/demo"
RC=$?
if [ "$RC" -ne 0 ]; then
    echo "---- server log ----"
    cat "$TMP/serve.log"
fi
exit "$RC"
