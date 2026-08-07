# libmusepack — stable decoder API

This document describes the stable, decoder-facing C API introduced in Phase 1
and frozen by the Phase 1 API/ABI audit. It is the canonical interface consumed
by the command-line tools, the WebAssembly wrapper, and future Swift (iOS) and
JNI (Android) wrappers.

Header layout:

```text
include/musepack/
├── musepack.h      umbrella header (include this one)
├── decoder.h       opaque musepack_decoder session API
├── reader.h        input abstraction (mpc_reader) + memory adapter
├── streaminfo.h    versioned musepack_stream_info
├── export.h        MUSEPACK_API / MUSEPACK_DEPRECATED / MUSEPACK_API_VERSION
└── version.h       generated library version macros
```

`#include <musepack/musepack.h>` pulls in everything.

## Concept

```text
FILE *  /  memory buffer  /  HTTP Range  /  custom source
                    |
                    v
                 mpc_reader            (callbacks + context)
                    |
                    v
         musepack_decoder               (opaque session)
                    |
                    v
          interleaved float PCM
```

The decoder core depends only on the reader abstraction. There is no global
mutable state and no input-buffering policy in the library beyond the
demuxer's internal read buffer. No explicit library initialization is
required.

## Reader abstraction (`reader.h`)

An `mpc_reader` is a struct of five callbacks plus a caller-owned context
pointer:

| Member      | Signature                                              | Returns                          |
|-------------|--------------------------------------------------------|----------------------------------|
| `read`      | `mpc_int32_t (*)(mpc_reader*, void*, mpc_int32_t)`     | bytes read (0 at end of input)   |
| `seek`      | `mpc_bool_t (*)(mpc_reader*, mpc_seek_t)`              | MPC_TRUE on success              |
| `tell`      | `mpc_seek_t (*)(mpc_reader*)`                          | current byte offset              |
| `get_size`  | `mpc_seek_t (*)(mpc_reader*)`                          | total size (0 if unknown)        |
| `canseek`   | `mpc_bool_t (*)(mpc_reader*)`                          | MPC_TRUE if seekable             |

Contract:

- `read` returns bytes read, `0` at end of available input; short reads are
  treated as end-of-available-data. A reader that cannot signal transport
  errors in-band returns `0` on failure, and the decoder reports the stream
  as truncated (`MUSEPACK_ERR_INVALID`).
- `seek`/`tell`/`get_size` use 64-bit `mpc_seek_t` — safe for inputs larger
  than 4 GiB.
- `get_size` returns `0` when the size is unknown (live streams); such
  readers should also return `MPC_FALSE` from `canseek`.
- Custom sources (HTTP Range, `.mpack` AU slices, browser fetch buffers,
  iOS/Android networking) carry their state in `data`; the decoder never
  touches it beyond passing it through.

Built-in adapters (all exported):

- `mpc_reader_init_stdio()` — opens and owns the `FILE *`; closed on exit.
- `mpc_reader_init_stdio_stream()` — borrows the caller's `FILE *` but the
  adapter still closes it on `mpc_reader_exit_stdio()`.
- `mpc_reader_init_memory()` — borrows the caller's buffer; only its own
  bookkeeping is freed on `mpc_reader_exit_memory()`.

The existing `mpc_reader` is retained as the permanent supported reader
interface: 64-bit positions, callback-based, context-pointer driven, already
consumed directly by the demux core, and suitable for HTTP Range, memory
slices and browser buffers. Its only weakness — `read` cannot distinguish a
transport error from end-of-input — is a documented core-decoder limitation
(short reads surface as `MUSEPACK_ERR_INVALID`), not an API blocker.

## Decoder session API (`decoder.h`)

```c
const char           *musepack_version(void);
musepack_decoder     *musepack_decoder_open(mpc_reader *reader, musepack_error *error_out);
void                  musepack_decoder_close(musepack_decoder *d);
musepack_error        musepack_decoder_get_stream_info(const musepack_decoder *d,
                                                       musepack_stream_info *out);
musepack_error        musepack_decoder_get_info(const musepack_decoder *d, mpc_streaminfo *out); /* legacy */
uint32_t              musepack_decoder_stream_version(const musepack_decoder *d);
uint32_t              musepack_decoder_sample_rate(const musepack_decoder *d);
uint32_t              musepack_decoder_channels(const musepack_decoder *d);
musepack_error        musepack_decoder_read(musepack_decoder *d, float *pcm,
                                            uint64_t max_frames, uint64_t *frames_out);
musepack_error        musepack_decoder_seek_sample(musepack_decoder *d, uint64_t sample);
musepack_error        musepack_decoder_seek_seconds(musepack_decoder *d, double seconds);
uint64_t              musepack_decoder_position(const musepack_decoder *d);
uint64_t              musepack_decoder_length_samples(const musepack_decoder *d);
musepack_error        musepack_decoder_check_stream(musepack_decoder *d);
```

### Stream information

`musepack_decoder_get_stream_info()` fills a versioned `musepack_stream_info`
(no internal pointers, all fixed-width fields). The caller sets
`info.size = sizeof(info)`; the library writes at most `size` bytes, so
consumers compiled against an older, smaller layout keep working with a newer
library (fields are only ever appended). `MUSEPACK_STREAM_INFO_MIN_SIZE` is
the v1 floor.

The legacy `musepack_decoder_get_info()` fills the historical `mpc_streaminfo`
(which contains `const char *profile_name` and file offsets) and is retained
for compatibility; new code uses the versioned structure.

### PCM and `read`

`read` returns interleaved single-precision float in sample-frame units (one
sample per channel), channel order L, R, L, R (stereo), range ~[-1, 1]. `pcm`
must hold at least `max_frames * channels` floats.

- `MUSEPACK_OK` — at least one frame written; `frames_out` equals
  `max_frames` unless the stream ended within the call.
- `MUSEPACK_ERR_EOF` — end of stream, `frames_out == 0`. EOF is a normal
  state, not an error, and every subsequent read keeps returning it until a
  seek.
- A failed read may still have produced valid frames (reported via
  `frames_out`); consume them before retrying. Errors do not invalidate the
  decoder.

### Seeking

Positions are 0-based sample-frames (per-channel frames) excluding gapless
leading silence. `seek_sample` clamps out-of-range values to the stream
length (seeking to the end yields immediate EOF); after a successful seek the
decode position is exactly the (clamped) target and `position()` reports it.
`seek_seconds` rounds half-up to the nearest frame and rejects negative
times. On a non-seekable reader (`canseek` false) both return
`MUSEPACK_ERR_SEEK`.

Clamping was chosen deliberately: scrubbing past the end is a normal player
action, and the target is still well-defined (end-of-stream). Explicit range
errors would add failure handling with no benefit.

## Error codes

| Code                     | Value | Meaning                                |
|--------------------------|-------|----------------------------------------|
| `MUSEPACK_OK`            |  0    | success                                |
| `MUSEPACK_ERR_INVALID`   | -1    | invalid stream/argument; current decoder also reports reader truncation as this |
| `MUSEPACK_ERR_IO`        | -2    | reserved for future readers that can distinguish transport failure |
| `MUSEPACK_ERR_NOMEM`     | -3    | out of memory                          |
| `MUSEPACK_ERR_SEEK`      | -4    | seek failed / source not seekable      |
| `MUSEPACK_ERR_EOF`       | -5    | end of stream reached                  |

## Ownership / lifecycle

- The caller owns the reader; the decoder borrows it for its lifetime and
  never frees it (or its `data`). Keep the reader alive until after `close()`.
- `musepack_decoder_close()` accepts NULL (no-op). Close twice or
  use-after-close is undefined behaviour.
- `open()` consumes header bytes from the reader; the reader's position after
  open is unspecified. A reader may be reused for another decoder only after
  being repositioned to the stream start.
- Failed decode/seek calls leave the decoder usable; the reader position is
  left at the point of failure.
- Memory-reader buffers must outlive their reader and any decoder on it.
- The stdio adapters close the underlying `FILE *` on exit (owning and
  borrowed variants alike).

## Thread-safety contract

Contractual, not incidental: distinct decoder instances (each with its own
reader) may be used concurrently from different threads. Huffman LUTs are
built once on first use (`pthread_once`, `InitOnceExecuteOnce` on Windows, a
single-threaded stub under Emscripten) so no explicit init is needed and the
first concurrent opens cannot race. A single decoder instance and its reader
are not thread-safe; a reader must not be shared across decoders on different
threads. Verified by a concurrent two-thread decode probe.

## Symbol / ABI hygiene

The library builds shared and (when shared is requested) static variants.
Internal symbols are hidden on ELF/Mach-O with `-fvisibility=hidden`; the
public surface is re-exported via `MUSEPACK_API` (new API) and `MPC_API`
(legacy headers). On Windows the shared build uses `__declspec(dllexport)`
and consumers get `dllimport` from the exported CMake target
(`MUSEPACK_USE_SHARED`). Only the following are exported:

- the `musepack_*` public API,
- the legacy `mpc_demux_*` / `mpc_decoder_*` / `mpc_reader_*` /
  `mpc_set_replay_level` / `mpc_streaminfo_*` public API,
- `mpc_bits_get_block` / `mpc_bits_get_size` (legacy `MPC_API` bit readers
  used by the in-tree SV8 block tools; slated to be removed once a public
  block API lands).

`MUSEPACK_API` is defined in `musepack/export.h`; `MUSEPACK_DEPRECATED` is
available for staged removals.

## Versioning / evolution strategy

- Library version: `PROJECT_VERSION` (7.0.1), `musepack_version()` returns it.
  `MUSEPACK_VERSION_MAJOR/MINOR/PATCH` come from the generated `version.h`.
- `MUSEPACK_API_VERSION` (integer, currently 1) bumps only on incompatible
  API changes; consumers can gate on it with preprocessor conditionals.
- `SOVERSION` equals the major version; a bump signals an ABI break.
- Evolution rules: append fields to `musepack_stream_info` (the `size` field
  keeps old binaries working); add new functions rather than changing
  signatures; deprecate with `MUSEPACK_DEPRECATED` and remove only across an
  `MUSEPACK_API_VERSION` bump. Both source and ABI compatibility are
  maintained within one SOVERSION.

## Swift / JNI suitability

The API is binding-friendly: fixed-width integer types throughout (no
`long`/`size_t` in the public surface), opaque handles with clear
lifetime, no returned pointers except `musepack_version()` (static, owned),
no structs containing internal pointers in the new API, simple constants
instead of macros with side effects, and reader callbacks that carry a
context pointer (a JNI/Swift bridge object can live in `data`). The versioned
`musepack_stream_info` imports directly as a C struct.

## WebAssembly

The decoder core compiles unchanged under Emscripten (single-threaded, no
SharedArrayBuffer, no experimental extensions). `wasm/musepack_wasm.c` is a
handle-based shim that calls only the canonical public API — no decoder
logic, no duplicated parsing, no browser-specific behavior. See
`wasm/musepack_wasm.c` and `wasm/CMakeLists.txt`.

```sh
emcmake cmake -S . -B build-wasm -DMPC_BUILD_TESTS=ON
cmake --build build-wasm -j
ctest --test-dir build-wasm -R wasm_smoke
```

Browser playback demo: see `demo/README.md`.

## Implementation notes

- `musepack_decoder.c` is a thin facade over the existing opaque `mpc_demux`
  interface; it adds buffered frame-wise PCM reading, position tracking and
  lifecycle formalization only. No codec logic lives there.
- Frames decoded during a seek's sample-skip can legitimately carry zero
  samples; the facade skips them transparently.
- The historical `<mpc/*.h>` headers remain installed unchanged; new code
  should use `<musepack/*.h>`.

## Sanitizer status

The decoder/encoder are ASan+UBSan-clean over the regression corpus, and all
suites (`unit`, `api`, `fixtures`, `integration`, `fuzz`, `wasm_smoke`) run
clean under sanitizers. The two stack-buffer-underflow findings reported at
the end of Phase 1 were both classified as bit-reader contract violations in
test/tool code (the bit reader reads up to four bytes before its current
pointer) and were fixed by honoring the documented buffer contract with
leading pad bytes in `tests/unit_tests.c` and `mpccut.c`. The demux satisfies
the contract by embedding its buffer in a larger zero-initialized struct, so
its backward reads stay inside the allocation; this is documented in
`libmpcdec/mpc_bits_reader.h`.
