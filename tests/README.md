# Musepack tests

All tests are registered with CTest and run with:

```sh
cmake -S . -B build -DMPC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Five suites are run:

| Test            | What it checks                                             |
|-----------------|------------------------------------------------------------|
| `unit`          | crc32 vectors, byte-aligned bit writer/reader round-trip,  |
|                 | size encode/decode round-trip, Cnk table math, Huffman LUTs|
| `fixtures`      | decode(fixture.mpc) == golden .wav (sample-exact)          |
| `integration`   | end-to-end encode/decode/seek/cut/compare on real files    |
| `fuzz`          | decoder survives truncated and bit-flipped inputs          |
| `compat`        | encoder output byte-identical to the reference encoder     |

The `unit` suite is a C program (`unit_tests.c`) and runs on all platforms.
`fixtures`, `integration`, `fuzz`, and `compat` are bash/python3 scripts and
are registered only on UNIX.

## Fixture regression harness

`run_tests.sh` decodes every `fixtures/*.mpc` with a freshly built `mpcdec`
and compares the output **sample-for-sample** against the golden
`fixtures/*.wav`.

Usage:

```sh
# Build in a temp dir and run
tests/run_tests.sh

# Reuse an existing build directory
tests/run_tests.sh /path/to/build

# Use a specific mpcdec binary
tests/run_tests.sh /path/to/build /path/to/build/mpcdec/mpcdec
```

### Regenerating fixtures

`generate_fixtures.py` synthesizes deterministic WAV signals, encodes them
with `mpcenc`, and decodes them with `mpcdec` to produce the golden outputs.
Run it with the reference (known-good) binaries whenever fixtures need to be
recreated:

```sh
python3 tests/generate_fixtures.py \
    --mpcenc build/mpcenc/mpcenc \
    --mpcdec  build/mpcdec/mpcdec
```

The golden files are committed so that decode output is pinned for
bit-exactness. Only regenerate them when the reference encoder/decoder has
intentionally changed.

## Integration test

`run_integration.sh <mpcenc> <mpcdec> <mpccut> <wavcmp>` generates a
deterministic WAV, encodes, decodes, checks integrity (`-c`), prints info
(`-i`), verifies the decoded sample count, cuts a range with `mpccut`, and
compares files with `wavcmp`.

## Fuzz-lite robustness

`run_fuzz.sh <mpcdec> <fixture.mpc>` truncates a valid file at every length
and bit-flips random bytes, then runs `mpcdec` (decode and `-c`) on each.
The decoder is expected to reject malformed input gracefully, never crash
(no signal exit status).

## Reference-encoder compatibility

`run_compat.sh <mpcenc>` regenerates the deterministic corpus from
`generate_corpus.py`, encodes every WAV at the quality levels pinned in
`reference_manifest.txt`, and verifies the SHA-256 of each output against
the manifest. The manifest was produced by the pristine upstream encoder
(r475 / git `05d97a5`), so a passing `compat` test proves the built encoder
is byte-identical to the reference.

Note: encoder byte-identity is optimization-dependent (verified at both
`-O0` and `-O3` against the reference). The manifest is generated from a
`-O0` reference build, matching the default CMake build type used by the CI
UNIX test build. Regenerate the manifest from a reference build at the same
optimization as the encoder under test if you change the build type.

