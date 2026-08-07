#!/usr/bin/env bash
# Robustness tests: the decoder must not crash on malformed input.
#
# Truncates a valid MPC file at every length and bit-flips random bytes,
# then runs mpcdec (decoding + -c) on each. Any non-zero exit that is NOT
# the expected "invalid input" failure is treated as a crash.
#
# Usage: tests/run_fuzz.sh <mpcdec> <fixture.mpc>
# Exit status: 0 if the decoder survived all inputs.

set -u

MPCDEC="${1:?mpcdec path}"
SRC="${2:?fixture .mpc path}"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mpc-fuzz.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

SIZE="$(stat -f%z "$SRC" 2>/dev/null || stat -c%s "$SRC")"
CRASHES=0

echo "== robustness (fuzz-lite) =="

# 1. Truncation: decode a prefix of every length. The decoder is expected to
#    fail gracefully (mpcdec returns non-zero), but never crash (139/134/11).
for (( len = 1; len <= SIZE; len += SIZE / 20 )); do
    [ "$len" -lt 1 ] && len=1
    head -c "$len" "$SRC" > "$TMP/trunc.mpc"
    "$MPCDEC" "$TMP/trunc.mpc" "$TMP/trunc.wav" >/dev/null 2>&1
    rc=$?
    if [ $rc -ge 128 ] && [ $rc -ne 255 ]; then
        echo "CRASH on truncation at $len bytes (rc=$rc)"
        CRASHES=$((CRASHES + 1))
    fi
done

# 2. Random byte flips in valid-length files.
python3 - "$SRC" "$TMP" <<'EOF'
import os, random, sys
src, tmp = sys.argv[1], sys.argv[2]
data = bytearray(open(src, "rb").read())
rng = random.Random(1234)
for i in range(200):
    d = bytearray(data)
    for _ in range(1 + rng.randrange(4)):
        d[rng.randrange(len(d))] ^= 1 << rng.randrange(8)
    with open(os.path.join(tmp, "flip_%03d.mpc" % i), "wb") as f:
        f.write(bytes(d))
EOF

for f in "$TMP"/flip_*.mpc; do
    "$MPCDEC" "$f" "$TMP/f.wav" >/dev/null 2>&1
    rc=$?
    if [ $rc -ge 128 ] && [ $rc -ne 255 ]; then
        echo "CRASH on $(basename "$f") (rc=$rc)"
        CRASHES=$((CRASHES + 1))
    fi
    # -c mode too
    "$MPCDEC" -c "$f" >/dev/null 2>&1
    rc=$?
    if [ $rc -ge 128 ] && [ $rc -ne 255 ]; then
        echo "CRASH on -c $(basename "$f") (rc=$rc)"
        CRASHES=$((CRASHES + 1))
    fi
done

echo
if [ "$CRASHES" -eq 0 ]; then
    echo "== no crashes on malformed input =="
    exit 0
fi
echo "== $CRASHES crash(es) detected =="
exit 1
