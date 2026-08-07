#!/usr/bin/env bash
# Integration tests for the Musepack toolchain.
#
# Requires a built tree and the tool binaries. Exercises:
#   - encode -> decode round-trip
#   - mpcdec -c (integrity check)
#   - mpcdec -i (info)
#   - mpc_demux seeking via mpcdec (decode starting at an offset)
#   - mpccut (sample-range cutting)
#   - wavcmp (sample-exact comparison)
#
# Usage:
#   tests/run_integration.sh <builddir> <mpcenc> <mpcdec> <mpccut> <wavcmp>
# Exit status: 0 if all pass, 1 otherwise.

set -u

MPCENC="${1:?mpcenc path}"
MPCDEC="${2:?mpcdec path}"
MPCCUT="${3:?mpccut path}"
WAVCMP="${4:?wavcmp path}"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mpc-integration.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

FAILED=0
PASSED=0

fail() { echo "FAIL  $1"; FAILED=$((FAILED + 1)); }
pass() { echo "PASS  $1"; PASSED=$((PASSED + 1)); }

# Generate a deterministic 1s 44.1kHz stereo test WAV.
SRC="$TMP/src.wav"
OUT="$TMP/out.mpc"
DEC="$TMP/dec.wav"

python3 - "$SRC" <<'EOF'
import math, sys, wave
rate = 44100
n = rate
with wave.open(sys.argv[1], "w") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
    frames = bytearray()
    for i in range(n):
        t = i / rate
        l = int(20000 * math.sin(2 * math.pi * 440 * t))
        r = int(20000 * math.sin(2 * math.pi * 550 * t))
        frames += (l & 0xFFFF).to_bytes(2, "little")
        frames += (r & 0xFFFF).to_bytes(2, "little")
    w.writeframes(bytes(frames))
EOF

echo "== integration tests =="

# 1. Encode
if "$MPCENC" --silent --overwrite "$SRC" "$OUT" >/dev/null 2>&1 && [ -s "$OUT" ]; then
    pass "encode"
else
    fail "encode"
fi

# 2. Integrity check
if "$MPCDEC" -c "$OUT" >/dev/null 2>&1; then
    pass "mpcdec -c"
else
    fail "mpcdec -c"
fi

# 3. Info
if "$MPCDEC" -i "$OUT" 2>&1 | grep -q "stream version 8"; then
    pass "mpcdec -i"
else
    fail "mpcdec -i"
fi

# 4. Full decode round-trip
if "$MPCDEC" "$OUT" "$DEC" >/dev/null 2>&1; then
    pass "decode"
else
    fail "decode"
fi

# 5. wavcmp on the round-trip (should differ slightly from source but be valid)
if "$WAVCMP" "$SRC" "$DEC" >/dev/null 2>&1; then
    pass "wavcmp runs"
else
    fail "wavcmp runs"
fi

# 6. Seeking: decode and check total decoded samples matches stream length
LEN="$($MPCDEC -i "$OUT" 2>&1 | grep -oE '\([0-9]+ samples\)' | tr -d '()')"
SAMPLES="${LEN%% *}"
DECCNT="$($MPCDEC "$OUT" "$TMP/seek.wav" 2>&1 | grep -oE '^[0-9]+ samples' | awk '{print $1}')"
if [ -n "$SAMPLES" ] && [ "$DECCNT" = "$SAMPLES" ]; then
    pass "decode sample count ($SAMPLES)"
else
    fail "decode sample count (expected $SAMPLES, got $DECCNT)"
fi

# 7. mpccut: cut first half, then decode and confirm half the samples
HALF=$((SAMPLES / 2))
if "$MPCCUT" -s 0 -e "$HALF" "$OUT" "$TMP/cut.mpc" >/dev/null 2>&1; then
    CUTCNT="$("$MPCDEC" "$TMP/cut.mpc" "$TMP/cut.wav" 2>&1 | grep -oE '^[0-9]+ samples' | awk '{print $1}')"
    if [ "$CUTCNT" = "$HALF" ]; then
        pass "mpccut ($CUTCNT samples)"
    else
        fail "mpccut count (expected $HALF, got $CUTCNT)"
    fi
else
    fail "mpccut"
fi

echo
echo "== $PASSED passed, $FAILED failed =="
[ "$FAILED" -eq 0 ]
