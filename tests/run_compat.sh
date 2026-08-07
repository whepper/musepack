#!/usr/bin/env bash
# Compatibility test: the built encoder must be byte-identical to the
# pristine reference encoder (r475 / git 05d97a5).
#
# Two modes:
#   1. Live comparison (CI): a reference mpcenc is provided (2nd argument or
#      REF_MPCENC env) and the expected hashes are produced by encoding the
#      corpus with it on the spot. This is toolchain-agnostic and is how the
#      test runs on all CI platforms.
#   2. Manifest comparison (local fallback): with no reference binary, compare
#      against tests/reference_manifest.txt. That manifest is pinned to a -O0
#      reference build, so only trust it at matching optimization.
#
# Usage:
#   tests/run_compat.sh <mpcenc> [ref_mpcenc] [corpus_dir]
#
# Exit status: 0 if all outputs match, 1 otherwise.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$ROOT/tests/reference_manifest.txt"
MPCENC="${1:-}"
REF_MPCENC="${2:-${REF_MPCENC:-}}"
CORPUS_DIR="${3:-}"

if [ -z "$MPCENC" ] || [ ! -x "$MPCENC" ]; then
    echo "ERROR: usage: tests/run_compat.sh <path-to-mpcenc> [ref_mpcenc] [corpus_dir]" >&2
    exit 1
fi

# Python is required for corpus generation; use it for hashing too so we do
# not depend on shasum/sha256sum being present (Windows Git Bash has them,
# but python is guaranteed).
PY=""
for c in python3 python; do
    if command -v "$c" >/dev/null 2>&1; then PY="$c"; break; fi
done
if [ -z "$PY" ]; then
    echo "ERROR: python3/python not found on PATH" >&2
    exit 1
fi

sha256() { "$PY" -c 'import sys,hashlib; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mpc-compat.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

if [ -z "$CORPUS_DIR" ]; then
    CORPUS_DIR="$TMP/corpus"
    "$PY" "$ROOT/tests/generate_corpus.py" "$CORPUS_DIR" >/dev/null
fi

QUALITIES="3 5 7"

# Produce the expected-hash manifest: <name> <quality> <sha256> per line.
if [ -n "$REF_MPCENC" ]; then
    if [ ! -x "$REF_MPCENC" ]; then
        echo "ERROR: reference mpcenc not executable: '$REF_MPCENC'" >&2
        exit 1
    fi
    echo "== comparing against reference encoder: $REF_MPCENC =="
    EXPECTED="$TMP/expected.txt"
    : > "$EXPECTED"
    for wav in "$CORPUS_DIR"/*.wav; do
        [ -e "$wav" ] || continue
        name="$(basename "$wav")"
        for q in $QUALITIES; do
            "$REF_MPCENC" --silent --overwrite --quality "$q" "$wav" "$TMP/ref.mpc" >/dev/null 2>&1
            echo "$name $q $(sha256 "$TMP/ref.mpc")" >> "$EXPECTED"
        done
    done
else
    echo "== comparing against committed manifest: $MANIFEST =="
    EXPECTED="$MANIFEST"
fi

FAILED=0
TOTAL=0
for wav in "$CORPUS_DIR"/*.wav; do
    [ -e "$wav" ] || continue
    name="$(basename "$wav")"
    for q in $QUALITIES; do
        expected="$(awk -v n="$name" -v qq="$q" '$1==n && $2==qq {print $3; exit}' "$EXPECTED")"
        if [ -z "$expected" ]; then
            continue
        fi
        TOTAL=$((TOTAL + 1))
        "$MPCENC" --silent --overwrite --quality "$q" "$wav" "$TMP/out.mpc" >/dev/null 2>&1
        if [ $? -ne 0 ]; then
            echo "FAIL  $name q=$q (encode returned non-zero)"
            FAILED=$((FAILED + 1))
            continue
        fi
        got="$(sha256 "$TMP/out.mpc")"
        if [ "$got" = "$expected" ]; then
            echo "PASS  $name q=$q"
        else
            echo "FAIL  $name q=$q (expected $expected, got $got)"
            FAILED=$((FAILED + 1))
        fi
    done
done

echo
echo "== $((TOTAL - FAILED))/$TOTAL outputs match the reference encoder =="
[ "$FAILED" -eq 0 ]
