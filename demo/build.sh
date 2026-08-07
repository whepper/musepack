#!/usr/bin/env bash
# Build the libmusepack WASM module and copy it into the demo directory.
# Requires Emscripten (emcmake) on PATH.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEMO="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build-wasm"

emcmake cmake -S "$ROOT" -B "$BUILD"
cmake --build "$BUILD" --target musepack_wasm -j

cp "$BUILD/wasm/musepack.js" "$BUILD/wasm/musepack.wasm" "$DEMO/"
echo "copied musepack.js + musepack.wasm into $DEMO"
