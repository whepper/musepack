#!/usr/bin/env bash
# Fuzz-lite for the libmusicpack tag readers (Phase 3A): truncations and
# bit-flips of a valid FLAC metadata fixture run through
# `mpc_mpack_tests --parse-meta`. The parser must reject every input without
# crashing (a crash exits >= 128 via a signal).
#
# Usage: tests/run_meta_fuzz.sh <mpack-tests-bin> <meta-fixtures-dir>

set -u

TESTS="${1:?mpack_tests binary}"
META="${2:?meta fixtures dir}"

SRC="$META/album-vorbis.flac"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/meta-fuzz.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

PASSED=0
run_case() {
    "$TESTS" --parse-meta "$TMP/cur.flac" >/dev/null 2>&1
    rc=$?
    if [ "$rc" -ge 128 ]; then
        echo "CRASH on $1"
        exit 1
    fi
    PASSED=$((PASSED + 1))
}

# truncations at sampled byte boundaries
LEN=$(wc -c < "$SRC")
i=1
while [ "$i" -le "$LEN" ]; do
    head -c "$i" "$SRC" > "$TMP/cur.flac" 2>/dev/null
    run_case "truncate@$i"
    i=$((i + 113))
done

# bit-flips at random offsets (deterministic seed)
for k in $(seq 1 30); do
    python3 - "$SRC" "$TMP/cur.flac" "$k" <<'EOF'
import random, sys
data = bytearray(open(sys.argv[1], "rb").read())
rng = random.Random(5000 + int(sys.argv[3]))
for _ in range(4):
    i = rng.randrange(len(data))
    data[i] ^= (1 << rng.randrange(8))
open(sys.argv[2], "wb").write(bytes(data))
EOF
    run_case "bitflip#$k"
done

# the other fixtures must also parse (or reject) without crashing
for f in notags.flac bad-magic.flac truncated.flac oversized.flac; do
    cp "$META/$f" "$TMP/cur.flac"
    run_case "fixture:$f"
done

echo "meta fuzz: $PASSED cases, no crashes"
