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

echo
echo "== $PASSED passed, $FAILED failed =="
[ "$FAILED" -eq 0 ]
