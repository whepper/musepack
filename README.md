# Musepack (MPC)

Musepack is an open-source, high-quality lossy audio codec. This repository
contains the decoder (`libmpcdec`), the encoder core (`libmpcenc`,
`libmpcpsy`), a small WAV I/O helper (`libwavformat`), and a set of command
line tools.

Stream formats **SV7** and **SV8** are supported by the decoder. The encoder
produces SV8.

## Building

CMake 3.16+ is required. All tools build out of the box:

```sh
cmake -S . -B build
cmake --build build -j
```

Useful options:

| Option               | Default             | Description                                  |
|----------------------|---------------------|----------------------------------------------|
| `MPC_BUILD_SHARED`   | `ON` (non-Windows)  | Build `libmusepack` as a shared library      |
| `MPC_BUILD_TESTS`    | `OFF`               | Register the regression suites (ctest)       |
| `MPC_BUILD_MPCGAIN`  | `ON`                | Build `mpcgain` (needs libreplaygain)        |
| `MPC_BUILD_MPCCHAP`  | `ON`                | Build `mpcchap` (needs libcuefile)           |

`mpcgain` and `mpcchap` are skipped automatically if their optional
dependencies (`libreplaygain`, `libcuefile`) are not found.

Install (headers, libraries, pkg-config files, and tools):

```sh
cmake --install build
```

Installed consumers can use the packaged CMake targets:

```cmake
find_package(Musepack CONFIG REQUIRED)
target_link_libraries(app PRIVATE Musepack::Decoder)
```

## libmusepack decoder API

`libmusepack` is the stable decoder-facing library (SV7/SV8). It exposes an
opaque, single-threaded session API over a pluggable reader abstraction
(files, memory buffers, or custom callbacks):

```c
#include <musepack/musepack.h>

mpc_reader reader;
mpc_reader_init_stdio(&reader, "song.mpc");
musepack_decoder *d = musepack_decoder_open(&reader, 0);
float pcm[MUSEPACK_FRAME_MAX * 2];
uint64_t n;
while (musepack_decoder_read(d, pcm, MUSEPACK_FRAME_MAX, &n) == MUSEPACK_OK) {
    /* play n sample-frames of interleaved float PCM */
}
musepack_decoder_close(d);
mpc_reader_exit_stdio(&reader);
```

See `docs/api.md` for the full API, ownership rules, error codes, and the
reader design. The decoder also builds to WebAssembly (Emscripten) and ships
with a browser playback demo:

- WASM: `emcmake cmake -S . -B build-wasm && cmake --build build-wasm`
- Demo: `demo/README.md` (build, serve, play an `.mpc` in a browser)

## Tools

| Tool       | Purpose                                                        |
|------------|----------------------------------------------------------------|
| `mpcdec`   | Decode MPC to WAV; `-i` prints stream info, `-c` checks a file |
| `mpcenc`   | Encode WAV (or other formats via external decoders) to MPC     |
| `mpc2sv8`  | Convert SV7 files to SV8                                       |
| `mpccut`   | Cut a range of samples out of an SV8 file                      |
| `mpcgain`  | Compute ReplayGain (requires libreplaygain)                    |
| `mpcchap`  | Read/write SV8 chapters (requires libcuefile)                  |
| `wavcmp`   | Compare two WAV files                                          |

## Testing

See `tests/README.md`. With `-DMPC_BUILD_TESTS=ON`, `ctest` runs six native
suites: unit tests (crc32, bitstream, tables), the libmusepack API suite
(lifecycle, invalid input, memory/file decoding, seeking, multiple
instances), a fixture regression that pins decode output for bit-exactness,
end-to-end integration, decoder robustness on malformed input, and a
compatibility check that pins the encoder to bit-identical output with the
pristine reference encoder (compared live against a reference build on CI,
with a committed manifest as the local fallback). The Emscripten build
registers an additional `wasm_smoke` suite:

```sh
cmake -S . -B build -DMPC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

## Layout

- `include/mpc/` — historical public headers (installed)
- `include/musepack/` — stable `libmusepack` public API
- `libmpcdec/` — SV7/SV8 decoder + `musepack_decoder` facade
- `libmpcenc/` — SV8 encoder core
- `libmpcpsy/` — psychoacoustic model
- `libwavformat/` — WAV read/write helper
- `wasm/` — Emscripten build of the decoder + WASM wrapper + smoke test
- `demo/` — browser playback proof-of-concept
- `common/` — shared sources (crc32, fast-math tables, tag handling)
- `tests/` — fixture generator, corpus generator, and regression harnesses
- `legacy/` — retired autotools and Visual Studio 2005 build files

## License

`libmpcdec` is BSD-licensed; `libmpcenc`, `libmpcpsy`, and the tools are
LGPL-licensed. See `LICENSE` for details.
