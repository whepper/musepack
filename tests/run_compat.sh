#!/usr/bin/env bash
# Compatibility manifest test.
#
# Encodes the deterministic synthetic corpus (tests/generate_corpus.py) at a
# set of quality levels with a freshly built mpcenc and verifies the SHA-256
# of every output against the reference-encoder manifest
# (tests/reference_manifest.txt). This pins the modernized encoder to
# bit-identical output with the pristine upstream encoder (r475 / 05d97a5).
#
# Usage:
#   tests/run_compat.sh <mpcenc> [corpus_dir]
#
# Exit status: 0 if all outputs match the reference manifest, 1 otherwise.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$ROOT/tests/reference_manifest.txt"
MPCENC="${1:-}"
CORPUS_DIR="${2:-}"

if [ -z "$MPCENC" ] || [ ! -x "$MPCENC" ]; then
    echo "ERROR: usage: tests/run_compat.sh <path-to-mpcenc> [corpus_dir]" >&2
    exit 1
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mpc-compat.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

if [ -z "$CORPUS_DIR" ]; then
    CORPUS_DIR="$TMP/corpus"
    python3 "$ROOT/tests/generate_corpus.py" "$CORPUS_DIR" >/dev/null
fi

QUALITIES="$(awk '{print $2}' "$MANIFEST" | sort -u | tr '\n' ' ')"

FAILED=0
TOTAL=0
for wav in "$CORPUS_DIR"/*.wav; do
    [ -e "$wav" ] || continue
    name="$(basename "$wav")"
    for q in $QUALITIES; do
        expected="$(awk -v n="$name" -v qq="$q" '$1==n && $2==qq {print $3; exit}' "$MANIFEST")"
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
        got="$(shasum -a 256 "$TMP/out.mpc" | awk '{print $1}')"
        if [ "$got" = "$expected" ]; then
            echo "PASS  $name q=$q"
        else
            echo "FAIL  $name q=$q (expected $expected, got $got)"
            FAILED=$((FAILED + 1))
        fi
    done
done

echo
echo "== $((TOTAL - FAILED))/$TOTAL outputs match reference manifest =="
[ "$FAILED" -eq 0 ]
