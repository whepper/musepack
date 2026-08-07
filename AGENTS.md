# AGENTS.md

Guidance for AI agents working on this Musepack (MPC) codebase. Read this
before making changes.

## What this repository is

Musepack is an open-source lossy audio codec. This repo contains the decoder
(`libmpcdec`), the encoder core (`libmpcenc`, `libmpcpsy`), a WAV helper
(`libwavformat`), shared sources (`common/`), and the CLI tools (`mpcdec`,
`mpcenc`, `mpc2sv8`, `mpccut`, `mpcgain`, `mpcchap`, `wavcmp`). The decoder
supports SV7/SV8; the encoder produces SV8.

This is a **modernized** copy of upstream Musepack r475 (git `05d97a5`).
The working rule is: **modernize the ecosystem aggressively, change the codec
conservatively.** Any risky codec change must be proven behavior-preserving.

## Building

CMake 3.16+. Configure, build, test:

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build
```

Options (see CMakeLists.txt):

| Option               | Default            | Description                              |
|----------------------|--------------------|------------------------------------------|
| `MPC_BUILD_SHARED`   | `ON` (non-Windows) | Build `libmpcdec` as a shared library    |
| `MPC_BUILD_TESTS`    | `OFF`              | Register the CTest suites                |
| `MPC_BUILD_MPCGAIN`  | `ON`               | `mpcgain` (needs libreplaygain)          |
| `MPC_BUILD_MPCCHAP`  | `ON`               | `mpcchap` (needs libcuefile)             |

Enable tests with `-DMPC_BUILD_TESTS=ON`. Local CI runs on Linux/macOS/Windows.

## Tests

Five CTest suites, all under `tests/`:

| Suite         | What it checks                                             |
|---------------|------------------------------------------------------------|
| `unit`        | crc32, bit writer/reader, size codes, Cnk tables, Huffman  |
| `fixtures`    | decode(fixture.mpc) ≈ golden .wav (tolerance ±2 LSB)       |
| `integration` | end-to-end encode/decode/seek/cut/compare                  |
| `fuzz`        | decoder survives truncated/bit-flipped input               |
| `compat`      | encoder output byte-identical to reference encoder (manifest) |

Key details:

- `fixtures`, `integration`, `fuzz`, `compat` are bash/python3 and only
  registered on UNIX. `unit` runs on all platforms.
- **Fixture comparison is tolerance-based, not byte-exact.** `mpcdec -i`
  uses `pow`/`log10` (ReplayGain), and cross-platform libm/codegen can flip
  a 16-bit sample by ±1 LSB. Use `tests/wavcmp_tol.py` for comparisons.
- **Encoder byte-identity is optimization-dependent.** The `compat` manifest
  (`tests/reference_manifest.txt`) is pinned to a `-O0` reference build, which
  matches the default CMake build type of the CI UNIX test build. If you change
  the build type, regenerate the manifest from the reference at the same
  optimization before trusting `compat`.

## The reference encoder and `compat`

`compat` regenerates a deterministic corpus (`tests/generate_corpus.py`),
encodes with the built `mpcenc`, and compares SHA-256 against
`tests/reference_manifest.txt` (produced from pristine r475).

To regenerate the manifest, build the reference commit in a separate worktree
and encode over the corpus:

```sh
git worktree add --detach /tmp/musepack-ref 05d97a5
# reference CMakeLists hardcodes CMAKE_C_FLAGS ("-O3 ..."); it OVERRIDES any
# -DCMAKE_C_FLAGS you pass. Edit that line in the worktree copy to change
# optimization/flag settings.
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -S /tmp/musepack-ref -B /tmp/ref-build
cmake --build /tmp/ref-build -j --target mpcenc
python3 tests/generate_corpus.py /tmp/corpus
# then SHA-256 every encode at Q{3,5,7} -> tests/reference_manifest.txt
```

Verified facts (from the compatibility audit):
- Reference vs modernized are byte-identical at matched optimization
  (`-O0` and `-O3`, 0/693 differing either way).
- `-O0` vs `-O3` outputs differ in a few cases (e.g. `clipping_edge` q7) —
  floating-point codegen, not code deltas.
- FAST_MATH scope was narrowed from global (reference) to `mpcpsy`/`mpcenc`
  only (modernized) with zero effect: `libmpcenc` contains no FAST_MATH-guarded
  code.

## Undefined behavior

Both of these existed in pristine r475 and were fixed in the modernization
audit; do not reintroduce them:

- `libmpcpsy/psy_tab.c` — `(int)(BandWidth * 64. / SampleFreq)` with 0/0 at
  init (NaN cast, clamped by the 1..31 guards). Now guarded: the division is
  only evaluated when `SampleFreq != 0`.
- `libmpcdec/mpc_bits_reader.h` — left shift of a signed byte by 24
  (`r->buff[-3] << 24`, `r->buff[-4] << (32 - count)`) and `1 << n` in
  `mpc_bits_enum_dec`. Now shifted in `mpc_uint32_t`/`1u`.
- `libmpcpsy/profile.c::SetQualityParams` — `Profiles[i+1]` read past the
  table at `qual == 10` (index 16 of 16). Now uses a clamped second index.
- `libmpcenc/analy_filter.c::Vectoring` (FASTER path) — `c1 = Ci_opt - 8`
  formed a pointer before the array. Now uses post-increment with in-bounds
  initial pointers (trace-identical).
- `libmpcenc/encode_sv7.c::writeBitstream_SV8` — `idx <<= 1` over a signed
  int. Now `unsigned int idx`.

All fixes are proven byte-identical to the reference encoder (0/693 encodes
differ at both `-O0` and `-O3`) and sanitizer-clean (ASan+UBSan, 63 corpus
files × multiple qualities, encoder and decoder).

When running sanitizer builds, note the reference CMakeLists hardcodes
`CMAKE_C_FLAGS`, silently overriding `-DCMAKE_C_FLAGS` — instrument the
reference by editing its worktree CMakeLists, or the "clean" result is a false
negative.

## Codec internals worth knowing

- `mpcenc/mpcenc.c::Quantisierung` uses per-channel static error buffers
  `errorL`/`errorR` (`[32][36 + MAX_NS_ORDER]`). The upstream code had a
  copy-paste bug: the R-channel no-noise-shaping branch passed `errorL[Band]`
  instead of `errorR[Band]`. This is a **real latent bug with zero observable
  effect** (the error-buffer carry is dead: every
  `QuantizeSubbandWithNoiseShaping` memsets `errors[0..5]` first, so the
  carry is never consumed). The fix is correct — **keep it**, don't revert.
- `MAX_NS_ORDER = 6` (`libmpcpsy/libmpcpsy.h`). `NS_Order_L/R` are per-band,
  reset each frame in `NS_Analyse`, set by `FindOptimalANS`.
- `libmpcpsy` is reentrant (per-instance `psy_state_t` embedded in `PsyModel`);
  do not reintroduce file-scope mutable state.
- Combinatorial tables (`Cnk`/`Cnk_len`/`Cnk_lost`) live in
  `common/cnk_tables.h`, shared by decoder and encoder.

## Conventions

- C11, `-Wall -Wextra`, no warnings. No comments unless they explain a
  non-obvious decision (this repo's style is terse).
- Standard `<stdint.h>` types; use `common/fileio.h` helpers for 64-bit I/O
  (`fseeko`/`ftello`; `_fseeki64` on Windows).
- Keep `libmpcdec`/`libmpcenc` public-API-clean: tools should use the public
  headers in `include/mpc/`, not reach into private structs.
- `mpc_seek_t` is `mpc_uint64_t`; the reader interface uses it.
- Legacy autotools/VS2005 files live in `legacy/` (kept for reference, not
  part of the build).

## Git / workflow

- Only commit/push when asked. Stage only intended files; write concise
  commit messages matching the repo history.
- The repo has a working `.gitignore` (build dirs, `*.o`, etc.).
- Reference worktrees and audit scratch builds belong under
  `/var/folders/6h/83xm7ljx4sz5njxdhkjsq53h0000gn/T/opencode` on this machine;
  never commit temp/audit artifacts.
