# libmusepack — stable decoder API

This document describes the stable, decoder-facing C API introduced in Phase 1
and the surrounding library structure. It is the canonical interface consumed
by the command-line tools, the WebAssembly wrapper, and future Swift (iOS) and
JNI (Android) wrappers.

Header layout:

```text
include/musepack/
├── musepack.h      umbrella header (include this one)
├── decoder.h       opaque musepack_decoder session API
├── reader.h        input abstraction (mpc_reader) + memory adapter
└── streaminfo.h    mpc_streaminfo re-export
```

`#include <musepack/musepack.h>` pulls in all three.

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
demuxer's internal read buffer.

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

Built-in adapters:

- `mpc_reader_init_stdio()` / `mpc_reader_exit_stdio()` — file reader that
  owns its `FILE *`.
- `mpc_reader_init_stdio_stream()` — reader over a caller-provided `FILE *`.
- `mpc_reader_init_memory()` / `mpc_reader_exit_memory()` — reader over a
  caller-owned memory buffer (borrowed; never freed by the library).

Custom sources (HTTP Range, `.mpack` AU objects, fetch buffers, iOS/Android
networking) implement the five callbacks; the decoder is unchanged.

## Decoder session API (`decoder.h`)

```c
musepack_decoder *musepack_decoder_open(mpc_reader *reader, musepack_error *error_out);
void              musepack_decoder_close(musepack_decoder *d);
musepack_error    musepack_decoder_get_info(const musepack_decoder *d, mpc_streaminfo *out);
musepack_error    musepack_decoder_read(musepack_decoder *d, float *pcm,
                                        uint64_t max_frames, uint64_t *frames_out);
musepack_error    musepack_decoder_seek_sample(musepack_decoder *d, uint64_t sample);
musepack_error    musepack_decoder_seek_seconds(musepack_decoder *d, double seconds);
uint64_t          musepack_decoder_position(const musepack_decoder *d);
uint64_t          musepack_decoder_length_samples(const musepack_decoder *d);
musepack_error    musepack_decoder_check_stream(musepack_decoder *d);
```

`musepack_decoder` is fully opaque. `open` parses the header immediately and
returns NULL (with `*error_out` set) on invalid input. `read` returns
interleaved single-precision float PCM in sample-frame units (one sample per
channel each); `max_frames` bounds one call and `frames_out` reports how many
were written. At end of stream `read` returns `MUSEPACK_ERR_EOF` with
`frames_out == 0`. Seeking is sample-accurate (including gapless leading
silence) and out-of-range targets clamp to the stream length.

## Error codes

| Code                     | Value | Meaning                              |
|--------------------------|-------|--------------------------------------|
| `MUSEPACK_OK`            |  0    | success                              |
| `MUSEPACK_ERR_INVALID`   | -1    | invalid/unsupported stream or argument|
| `MUSEPACK_ERR_IO`        | -2    | reader I/O failure                   |
| `MUSEPACK_ERR_NOMEM`     | -3    | out of memory                        |
| `MUSEPACK_ERR_SEEK`      | -4    | seek failed / source not seekable    |
| `MUSEPACK_ERR_EOF`       | -5    | end of stream reached                |

## Ownership rules

- The caller owns the reader and its `data`; the decoder borrows it for its
  lifetime and never frees it. Keep the reader alive until after
  `musepack_decoder_close()`.
- `musepack_decoder_close()` releases the decoder only. Separate memory-reader
  buffers must outlive their reader.
- create/open/decode/seek/close may be repeated any number of times. Calling
  `close()` twice, or using a decoder after `close()`, is undefined behaviour.
- Errors do not invalidate the decoder; failed calls may be retried.

## Thread-safety

Separate decoder instances (with separate readers) are fully independent and
safe to use concurrently. Huffman LUTs are built once (`pthread_once`). A
single decoder instance is not thread-safe and must not be shared without
external locking.

## Library identity and packaging

The decoder library builds as `libmusepack` (output name `musepack`), with an
in-tree `mpcdec` alias target retained for the tools. `libmpcdec.pc` is still
installed for compatibility alongside the new `musepack.pc`.

```cmake
find_package(Musepack CONFIG REQUIRED)   # install tree
target_link_libraries(app PRIVATE Musepack::Decoder)
```

Exported CMake targets: `Musepack::Decoder` (the library) and
`Musepack::Headers` (public include dir). `MusepackConfig.cmake` runs
`find_dependency(Threads)`.

## WebAssembly

The same decoder core compiles under Emscripten (single-threaded, no
SharedArrayBuffer, no experimental extensions). See `wasm/musepack_wasm.c`
for the handle-based shim and `wasm/CMakeLists.txt` for the build flags.
Build with:

```sh
emcmake cmake -S . -B build-wasm -DMPC_BUILD_TESTS=ON
cmake --build build-wasm -j
ctest --test-dir build-wasm -R wasm_smoke
```

The node smoke test (`tests/run_wasm_smoke.sh`) decodes a golden fixture and
compares 16-bit PCM against the reference with the tolerance comparator.
Browser playback demo: see `demo/README.md`.

## Implementation notes

- `musepack_decoder.c` is a thin facade over the existing opaque `mpc_demux`
  interface; it adds buffered frame-wise PCM reading, position tracking and
  lifecycle formalization only. No codec logic lives there.
- Frames decoded during a seek's sample-skip can legitimately carry zero
  samples; the facade skips them transparently.
- The historical `<mpc/*.h>` headers remain installed unchanged; new code
  should use `<musepack/*.h>`.

## Known pre-existing sanitizer findings

The decoder/encoder are ASan+UBSan-clean over the regression corpus
(verified in Phase 0 and again in Phase 1). Two pre-existing ASan
stack-buffer-underflows remain in *tool/test* code and are carried forward
unchanged because the underlying `mpc_bits_read` intentionally reads up to
four bytes *before* its current byte pointer, which only the demux buffer
layout satisfies:

- `tests/unit_tests.c::test_bits_roundtrip` — starts a `mpc_bits_reader` at a
  bare stack buffer, so the backward reads cross into ASan's redzone.
- `mpccut.c` (main) — same pattern with a local SV8 block buffer.

Both are Phase 0 artifacts, unrelated to the libmusepack API, and fixing them
would change the bit-reader contract or pad tool buffers; they are left for a
dedicated cleanup phase.
