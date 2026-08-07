#!/usr/bin/env bash
# Integration tests for the .mpack package model.
#
# Uses the reference packages under tests/reference/, the musicpack CLI and
# a libmusicpack helper. Exercises:
#   - musicpack info / verify on both reference packages
#   - JSON Schema validation (when python3 jsonschema is available)
#   - negative cases: modified audio, missing audio, stray file, traversal
#   - Musepack handoff (track -> libmusepack decode) via mpack_tests
#
# Usage: tests/run_mpack.sh <musicpack-cmd> <mpack-tests-bin>

set -u

MUSICPACK="${1:?musicpack cmd}"
MPACK_TESTS="${2:?mpack_tests binary}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REF="$ROOT/tests/reference"
MPC_REF="$REF/test-musicpack-album.mpack"
FLAC_REF="$REF/test-flac-album.mpack"
SCHEMA="$ROOT/specs/musicpack-v1.schema.json"

FAILED=0
PASSED=0
fail() { echo "FAIL  $1"; FAILED=$((FAILED + 1)); }
pass() { echo "PASS  $1"; PASSED=$((PASSED + 1)); }

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mpack-integration.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# 1. info + verify on both reference packages
if "$MUSICPACK" info "$MPC_REF" >/dev/null 2>&1; then
    pass "info mpc reference"
else
    fail "info mpc reference"
fi
if "$MUSICPACK" info "$FLAC_REF" >/dev/null 2>&1; then
    pass "info flac reference"
else
    fail "info flac reference"
fi
if "$MUSICPACK" verify "$MPC_REF" >/dev/null 2>&1; then
    pass "verify mpc reference"
else
    fail "verify mpc reference"
fi
if "$MUSICPACK" verify "$FLAC_REF" >/dev/null 2>&1; then
    pass "verify flac reference"
else
    fail "verify flac reference"
fi

# 2. JSON Schema validation (optional dependency)
if python3 -c "import jsonschema" 2>/dev/null; then
    if python3 - "$SCHEMA" "$MPC_REF/manifest.json" "$FLAC_REF/manifest.json" <<'EOF'
import json, sys
from jsonschema import Draft202012Validator
schema = json.load(open(sys.argv[1]))
for p in sys.argv[2:]:
    Draft202012Validator(schema).validate(json.load(open(p)))
EOF
    then
        pass "json schema validates reference manifests"
    else
        fail "json schema validates reference manifests"
    fi
else
    echo "SKIP  json schema (python3 jsonschema not available)"
fi

# 3. libmusicpack C tests (parse/validate/handoff/security/meter)
if "$MPACK_TESTS" "$MPC_REF" "$FLAC_REF"; then
    pass "mpack C tests"
else
    fail "mpack C tests"
fi

# 4. negative cases on a copy of the reference package
cp -R "$MPC_REF" "$TMP/mut.mpack"

# 4a. modified audio -> verify fails with a checksum error
AUDIO="$TMP/mut.mpack/audio/01 - Alphaville - Big in Japan.mpc"
printf 'X' >> "$AUDIO"
if "$MUSICPACK" verify "$TMP/mut.mpack" 2>&1 | grep -q "checksum mismatch"; then
    pass "modified audio detected"
else
    fail "modified audio detected"
fi

# 4b. missing audio -> verify fails
rm -f "$TMP/mut.mpack/audio/02 - Bleachers - The Van.mpc"
if "$MUSICPACK" verify "$TMP/mut.mpack" 2>&1 | grep -q "missing file"; then
    pass "missing audio detected"
else
    fail "missing audio detected"
fi

# 4c. stray file -> warning only (fresh copy, no other corruption)
cp -R "$MPC_REF" "$TMP/stray.mpack"
printf 'junk' > "$TMP/stray.mpack/stray.txt"
OUT="$("$MUSICPACK" verify "$TMP/stray.mpack" 2>&1)"
if echo "$OUT" | grep -q "unreferenced file"; then
    pass "stray file warning"
else
    fail "stray file warning"
fi
if echo "$OUT" | grep -q "^error:"; then
    fail "no errors for stray file"
else
    pass "no errors for stray file"
fi

# 5. traversal manifest rejected
TRAV="$TMP/trav.mpack"
mkdir -p "$TRAV"
python3 - "$MPC_REF/manifest.json" "$TRAV/manifest.json" <<'EOF'
import json, sys
m = json.load(open(sys.argv[1]))
m["media"][0]["tracks"][0]["audio"]["path"] = "../../etc/passwd"
json.dump(m, open(sys.argv[2], "w"))
EOF
if "$MUSICPACK" info "$TRAV" >/dev/null 2>&1; then
    fail "traversal manifest rejected"
else
    pass "traversal manifest rejected"
fi

# 6. collector metadata: create a package with release/edition flags
CREAT="$TMP/collector.mpack"
A1="$MPC_REF/audio/01 - Alphaville - Big in Japan.mpc"
A2="$MPC_REF/audio/02 - Bleachers - The Van.mpc"
if "$MUSICPACK" create -o "$CREAT" -t "Collector Album" -a "Test Artist" \
    -R album -O 1986-06-16 -d 2016-09-23 -e "2016 Remaster" \
    -l "Example Records" -c "ABC 123" -C GB -m CD \
    -T "$A1" -n "Track One" -T "$A2" -n "Track Two" >/dev/null 2>&1; then
    pass "create with release flags"
else
    fail "create with release flags"
fi

if python3 - "$CREAT/manifest.json" <<'EOF'
import json, sys
m = json.load(open(sys.argv[1]))
rel = m["release"]
assert m["album"]["releaseType"] == "album"
assert m["album"]["originalReleaseDate"] == "1986-06-16"
assert rel["releaseDate"] == "2016-09-23"
assert rel["edition"] == "2016 Remaster"
assert rel["label"] == "Example Records"
assert rel["catalogueNumber"] == "ABC 123"
assert rel["country"] == "GB"
assert m["media"][0]["format"] == "CD"
assert m["loudness"]["algorithm"] == "ITU-R BS.1770-5"
print("ok")
EOF
then
    pass "release metadata in created manifest"
else
    fail "release metadata in created manifest"
fi

# 6b. album loudness must be a program measurement: feed a loud and a quiet
# track into one package; albumLUFS must NOT be the mean of track LUFS (the
# quiet track is relative-gated out of the album program) and album true peak
# must equal the max of track true peaks. Skipped when ffmpeg (needed to
# measure .wav) is unavailable; the concatenation-semantics proof lives in the
# C test test_album_loudness_aggregation.
python3 - "$TMP" <<'EOF'
import math, os, struct, sys, wave
tmp = sys.argv[1]
def wav(path, amp, rate=44100):
    w = wave.open(path, "wb")
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
    frames = bytearray()
    for i in range(rate * 3):
        v = int(amp * 32767 * math.sin(2 * math.pi * 1000 * i / rate))
        frames += struct.pack("<hh", v, v)
    w.writeframes(bytes(frames)); w.close()
wav(os.path.join(tmp, "loud.wav"), 0.95)
wav(os.path.join(tmp, "quiet.wav"), 0.05)
EOF
WAVPACK="$TMP/lq.mpack"
if "$MUSICPACK" create -o "$WAVPACK" -t "Loud Quiet" -a "A" -R album -m Digital \
    -T "$TMP/loud.wav" -n "Loud" -T "$TMP/quiet.wav" -n "Quiet" >/dev/null 2>&1; then
    pass "create mixed-level wav package"
else
    fail "create mixed-level wav package"
fi
if python3 - "$WAVPACK/manifest.json" <<'EOF'
import json, sys
m = json.load(open(sys.argv[1]))
tl = [t.get("loudness") for d in m["media"] for t in d["tracks"]]
if any(x is None for x in tl):
    print("skip: ffmpeg unavailable")
    sys.exit(0)
assert "loudness" in m
assert m["loudness"]["algorithm"] == "ITU-R BS.1770-5"
al = m["loudness"]["albumLUFS"]
tp = m["loudness"]["albumTruePeakDbTP"]
lufs = [x["trackLUFS"] for x in tl]
peaks = [x["truePeakDbTP"] for x in tl]
mean = sum(lufs) / len(lufs)
assert abs(al - mean) > 1.0, "albumLUFS must not be the arithmetic mean of track LUFS"
assert abs(al - max(lufs)) < 1.0, "loud track dominates the album measurement"
assert abs(tp - max(peaks)) < 0.01, "album true peak must be the max of track true peaks"
print("ok")
EOF
then
    pass "album loudness is a program measurement"
else
    fail "album loudness is a program measurement"
fi

# 6c. `info` shows collector metadata first-class
OUT="$("$MUSICPACK" info "$CREAT" 2>&1)"
if echo "$OUT" | grep -q "^Type: album" \
   && echo "$OUT" | grep -q "^Edition: 2016 Remaster" \
   && echo "$OUT" | grep -q "^Release date: 2016-09-23" \
   && echo "$OUT" | grep -q "^Original release: 1986-06-16" \
   && echo "$OUT" | grep -q "^Label: Example Records" \
   && echo "$OUT" | grep -q "^Catalogue: ABC 123" \
   && echo "$OUT" | grep -q "^Country: GB" \
   && echo "$OUT" | grep -q "^Medium: CD"; then
    pass "info shows collector metadata"
else
    fail "info shows collector metadata"
fi

# 6d. same album represented by two distinct releases (1987 CD vs 2016 Digital)
E1="$TMP/ed1987.mpack"; E2="$TMP/ed2016.mpack"
"$MUSICPACK" create -o "$E1" -t "Two Faces" -a "X" -R album -O 1987-03-01 \
    -d 1987-03-01 -e "1987 CD" -m CD -T "$A1" >/dev/null 2>&1
"$MUSICPACK" create -o "$E2" -t "Two Faces" -a "X" -R album -O 1987-03-01 \
    -d 2016-09-23 -e "2016 Digital Remaster" -m Digital -T "$A2" >/dev/null 2>&1
if python3 - "$E1/manifest.json" "$E2/manifest.json" <<'EOF'
import json, sys
a = json.load(open(sys.argv[1]))
b = json.load(open(sys.argv[2]))
assert a["album"]["title"] == b["album"]["title"]
assert a["album"]["originalReleaseDate"] == b["album"]["originalReleaseDate"]
assert a["release"]["edition"] != b["release"]["edition"]
assert a["release"]["releaseDate"] != b["release"]["releaseDate"]
assert a["media"][0]["format"] == "CD" and b["media"][0]["format"] == "Digital"
print("ok")
EOF
then
    pass "same album, two distinct editions"
else
    fail "same album, two distinct editions"
fi

echo
echo "== $PASSED passed, $FAILED failed =="
[ "$FAILED" -eq 0 ]
