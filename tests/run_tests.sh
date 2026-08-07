#!/usr/bin/env bash
# Musepack regression harness.
#
# Decodes every <name>.mpc in tests/fixtures with a freshly built mpcdec and
# compares the output sample-for-sample against the golden <name>.wav.
#
# Usage:
#   tests/run_tests.sh              # build via CMake, run all fixtures
#   tests/run_tests.sh <builddir>   # reuse an existing build directory
#
# Exit status: 0 if all fixtures pass, 1 otherwise.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIXTURES="$ROOT/tests/fixtures"
MPCDEC="${MPCDEC:-}"

BUILD=""
TMP=""
CREATED_BUILD=0
cleanup() {
    [ -n "$TMP" ] && rm -rf "$TMP"
    [ "$CREATED_BUILD" -eq 1 ] && rm -rf "$BUILD"
}
trap cleanup EXIT

if [ "$#" -ge 1 ]; then
    BUILD="$1"
else
    BUILD="$(mktemp -d "${TMPDIR:-/tmp}/mpc-test-build.XXXXXX")"
    CREATED_BUILD=1
fi

if [ "$#" -ge 2 ]; then
    MPCDEC="$2"
fi

echo "== running fixture regression tests =="

if [ -z "$MPCDEC" ]; then
    if [ -x "$BUILD/mpcdec/mpcdec" ]; then
        MPCDEC="$BUILD/mpcdec/mpcdec"
    elif [ -x "$BUILD/mpcdec/mpcdec_cmd" ]; then
        MPCDEC="$BUILD/mpcdec/mpcdec_cmd"
    else
        # Build first if needed (for the mktemp path)
        if [ -z "${_MPC_BUILT:-}" ]; then
            echo "-- configuring build in $BUILD"
            cmake -S "$ROOT" -B "$BUILD" -DMPC_BUILD_TESTS=OFF >/dev/null
            cmake --build "$BUILD" --target mpcdec -j >/dev/null
            _MPC_BUILT=1
            if [ -x "$BUILD/mpcdec/mpcdec" ]; then
                MPCDEC="$BUILD/mpcdec/mpcdec"
            elif [ -x "$BUILD/mpcdec/mpcdec_cmd" ]; then
                MPCDEC="$BUILD/mpcdec/mpcdec_cmd"
            fi
        fi
    fi
fi

if [ ! -x "$MPCDEC" ]; then
    echo "ERROR: mpcdec not found at '$MPCDEC'" >&2
    exit 1
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mpc-test-out.XXXXXX")"

FAILED=0
PASSED=0

for mpc in "$FIXTURES"/*.mpc; do
    [ -e "$mpc" ] || continue
    name="$(basename "$mpc" .mpc)"
    golden="$FIXTURES/$name.wav"
    if [ ! -e "$golden" ]; then
        echo "SKIP  $name (no golden $golden)"
        continue
    fi

    "$MPCDEC" "$mpc" "$TMP/$name.wav" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "FAIL  $name (mpcdec returned non-zero)"
        FAILED=$((FAILED + 1))
        continue
    fi

    if python3 "$ROOT/tests/wavcmp_tol.py" "$golden" "$TMP/$name.wav"; then
        echo "PASS  $name"
        PASSED=$((PASSED + 1))
    else
        echo "FAIL  $name (output deviates from golden)"
        FAILED=$((FAILED + 1))
    fi
done

echo
echo "== $PASSED passed, $FAILED failed =="
[ "$FAILED" -eq 0 ]
