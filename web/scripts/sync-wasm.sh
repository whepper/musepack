#!/usr/bin/env bash
# Copies the libmusepack WASM module and the shared demand-reader scripts into
# the client's public/ directory so the static build serves them verbatim.
#
# Sources:
#   build-wasm/wasm/musepack.{js,wasm}   Emscripten decoder module
#   demo/{reader_mailbox,rangereader,networker}.js   Phase 5 demand reader
#
# The copied files are gitignored; run `npm run sync:wasm` (automatic on
# `npm run dev` / `npm run build`) after building the wasm module.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WEB="$(cd "$(dirname "$0")/.." && pwd)"
PUB="$WEB/app/public"
WASM_BUILD="${WASM_BUILD:-$ROOT/build-wasm}"

mkdir -p "$PUB"

cp "$WASM_BUILD/wasm/musepack.js" "$PUB/musepack.js"
cp "$WASM_BUILD/wasm/musepack.wasm" "$PUB/musepack.wasm"
for f in reader_mailbox.js rangereader.js networker.js; do
  cp "$ROOT/demo/$f" "$PUB/$f"
done

echo "copied wasm module + demand reader into $PUB"
