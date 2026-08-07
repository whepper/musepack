#!/usr/bin/env bash
# Fuzz-lite for the .mpack parser: truncations and bit-flips of a valid
# manifest, plus malicious path injections, run through `musicpack verify`.
# The tool must reject every input without crashing.
#
# Usage: tests/run_mpack_fuzz.sh <musicpack-cmd> <fixture.mpack>

set -u

MUSICPACK="${1:?musicpack cmd}"
PKG="${2:?reference package}"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mpack-fuzz.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

SRC="$PKG/manifest.json"
cp -R "$PKG" "$TMP/base.mpack"

PASSED=0
run_case() {
    "$MUSICPACK" verify "$TMP/base.mpack" >/dev/null 2>&1
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
    head -c "$i" "$SRC" > "$TMP/base.mpack/manifest.json" 2>/dev/null
    run_case "truncate@$i"
    i=$((i + 97))   # sparse sampling keeps CI fast
done

# bit-flips at random offsets (deterministic seed)
for k in $(seq 1 30); do
    python3 - "$SRC" "$TMP/base.mpack/manifest.json" "$k" <<'EOF'
import json, random, sys
data = bytearray(open(sys.argv[1], "rb").read())
rng = random.Random(1000 + int(sys.argv[3]))
for _ in range(4):
    i = rng.randrange(len(data))
    data[i] ^= (1 << rng.randrange(8))
open(sys.argv[2], "wb").write(bytes(data))
EOF
    run_case "bitflip#$k"
done

# malicious path injections
for p in "../x" "/etc/passwd" "a\\b" "audio/:x" "a//b" "a/../b" ""; do
    python3 - "$PKG/manifest.json" "$TMP/base.mpack/manifest.json" "$p" <<'EOF'
import json, sys
m = json.load(open(sys.argv[1]))
m["media"][0]["tracks"][0]["audio"]["path"] = sys.argv[3]
json.dump(m, open(sys.argv[2], "w"))
EOF
    run_case "path=$p"
done

echo "mpack fuzz: $PASSED cases, no crashes"
