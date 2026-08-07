# Musepack tests

All tests are registered with CTest and run with:

```sh
cmake -S . -B build -DMPC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Five native suites are run (plus `wasm_smoke` in the Emscripten build):

| Test            | What it checks                                             |
|-----------------|------------------------------------------------------------|
| `unit`          | crc32 vectors, byte-aligned bit writer/reader round-trip,  |
|                 | size encode/decode round-trip, Cnk table math, Huffman LUTs|
| `api`           | libmusepack API: lifecycle, invalid input, memory/file     |
|                 | decoding, stream info, full decode, seeking, instances     |
| `fixtures`      | decode(fixture.mpc) == golden .wav (tolerance-based)       |
| `integration`   | end-to-end encode/decode/seek/cut/compare on real files    |
| `fuzz`          | decoder survives truncated and bit-flipped inputs          |
| `compat`        | encoder output byte-identical to the reference encoder     |
| `wasm_smoke`    | WASM build decodes a fixture; PCM ≈ golden WAV (Emscripten)|

The `unit` and `api` suites are C programs and run on all platforms.
`fixtures`, `integration`, and `fuzz` are bash/python3 scripts and are
registered only on UNIX. `compat` is a bash/python3 script that runs on all
platforms (Windows invokes it through Git Bash). `wasm_smoke` is registered
only when building with Emscripten.

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

`run_compat.sh <mpcenc> [ref_mpcenc]` regenerates the deterministic corpus
from `generate_corpus.py`, encodes every WAV at quality levels 3, 5 and 7,
and verifies the SHA-256 of each output against the pristine upstream
encoder (r475 / git `05d97a5`).

Two comparison modes:

* **Live (CI):** pass a reference `mpcenc` as the second argument or set the
  `REF_MPCENC` environment variable. The expected hashes are then produced by
  encoding the corpus with that reference binary on the spot. This is
  toolchain-agnostic — the reference and the encoder under test are built with
  the same compiler and flags on the CI runner — and is how the test runs on
  all three CI platforms (Linux, macOS, Windows).
* **Manifest (local fallback):** with no reference binary, compare against
  the committed `tests/reference_manifest.txt`. That manifest was produced by
  a `-O0` reference build and is only valid at matching optimization (the
  default CMake build type on the CI UNIX test build).

Note: encoder byte-identity is optimization-dependent (verified at both `-O0`
and `-O3` against the reference). With the live mode this is not an issue
because both binaries use the same settings; the manifest mode requires the
encoder under test to be built at the same optimization as the manifest.
Regenerate the manifest from a reference build at the same optimization as
the encoder under test if you change the build type.

