#!/usr/bin/env bash
# WASM smoke test: decode a known fixture in node via libmusepack.wasm and
# compare the PCM against the golden WAV with the tolerance comparator.
#
# Usage:
#   tests/run_wasm_smoke.sh <wasm-build-dir> [fixture.mpc]
# Requires node and python3.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WASM_BUILD="${1:?wasm build dir}"
FIXTURE="${2:-$ROOT/tests/fixtures/sine44-q5.mpc}"

MODULE_JS="$WASM_BUILD/wasm/musepack.js"
SMOKE_JS="$ROOT/wasm/smoke.js"
GOLDEN="${FIXTURE%.mpc}.wav"

[ -f "$MODULE_JS" ] || { echo "FAIL  wasm module not found at $MODULE_JS"; exit 1; }
[ -f "$SMOKE_JS" ] || { echo "FAIL  smoke.js not found"; exit 1; }

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mpc-wasm.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/decoded.wav"

if ! node "$SMOKE_JS" "$MODULE_JS" "$FIXTURE" "$OUT"; then
    echo "FAIL  wasm decode ($(basename "$FIXTURE"))"
    exit 1
fi

if ! python3 "$ROOT/tests/wavcmp_tol.py" "$GOLDEN" "$OUT"; then
    echo "FAIL  wasm PCM deviates from golden ($(basename "$FIXTURE"))"
    exit 1
fi

echo "PASS  wasm smoke ($(basename "$FIXTURE"))"
