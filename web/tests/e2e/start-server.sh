#!/usr/bin/env bash
# Starts a musicpack-server with a fixture library for the Playwright e2e
# suite. Builds the library (two albums, one with three editions), creates a
# token, and serves the built client from web/app/dist.
#
# The token is written to tests/e2e/.server-env.json for the tests to read.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
E2E="$(cd "$(dirname "$0")" && pwd)"
SERVER_BIN="${MUSICPACK_SERVER:-$ROOT/build/server/musicpack-server}"
STATIC_DIR="${MUSICPACK_STATIC_DIR:-$ROOT/web/app/dist}"
PORT="${MUSICPACK_E2E_PORT:-8099}"
PY="$(command -v python3 || command -v python)"

[ -x "$SERVER_BIN" ] || { echo "musicpack-server not found at $SERVER_BIN" >&2; exit 1; }
[ -f "$STATIC_DIR/index.html" ] || { echo "client build missing at $STATIC_DIR (npm run build)" >&2; exit 1; }

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mpack-e2e.XXXXXX")"
LIB="$TMP/lib"
DB="$TMP/lib.db"

"$PY" "$ROOT/tests/server_api_test.py" setup \
    "$ROOT/tests/reference/test-musicpack-album.mpack" \
    "$ROOT/tests/reference/test-flac-album.mpack" \
    "$TMP" || { echo "FAIL fixture setup" >&2; exit 1; }
LIB="$(cat "$TMP/libdir")"

"$SERVER_BIN" scan --library "$LIB" --database "$DB" >/dev/null 2>&1
TOKEN="$("$SERVER_BIN" token create --name "E2E" --database "$DB" 2>/dev/null | grep '^mpk_')"

# Build a "Long Player" album whose first track is the 48-second sine fixture,
# so the seek/fetch-accounting tests operate on a long enough stream.
"$PY" - "$ROOT/tests/reference/test-musicpack-album.mpack" "$LIB/Long Player.mpack" \
    "$ROOT/tests/fixtures/sine44-q5-48s.mpc" <<'PY'
import hashlib, json, os, shutil, sys
ref, dst, sine = sys.argv[1], sys.argv[2], sys.argv[3]
shutil.copytree(ref, dst)
sha = hashlib.sha256(open(sine, "rb").read()).hexdigest()
shutil.copy(sine, os.path.join(dst, "audio", "01 - Alphaville - Big in Japan.mpc"))
mpath = os.path.join(dst, "manifest.json")
m = json.load(open(mpath, encoding="utf-8"))
m["album"]["title"] = "Long Player"
m["album"]["originalReleaseDate"] = "1986-06-16"
m["release"]["edition"] = "1986 Original CD"
m["media"][0]["tracks"][0]["audio"]["sha256"] = sha
m["media"][0]["tracks"][0]["duration"] = 48
json.dump(m, open(mpath, "w", encoding="utf-8"), indent=1)
PY

"$SERVER_BIN" scan --library "$LIB" --database "$DB" >/dev/null 2>&1

cat > "$E2E/.server-env.json" <<JSON
{ "token": "$TOKEN", "baseUrl": "http://127.0.0.1:$PORT", "libdir": "$LIB" }
JSON

exec "$SERVER_BIN" serve --library "$LIB" --database "$DB" \
    --listen 127.0.0.1 --port "$PORT" --no-scan \
    --static-dir "$STATIC_DIR"
