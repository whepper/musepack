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
| `MPC_BUILD_SHARED`   | `ON` (non-Windows)  | Build `libmpcdec` as a shared library        |
| `MPC_BUILD_TESTS`    | `OFF`               | Register the fixture regression tests (ctest)|
| `MPC_BUILD_MPCGAIN`  | `ON`                | Build `mpcgain` (needs libreplaygain)        |
| `MPC_BUILD_MPCCHAP`  | `ON`                | Build `mpcchap` (needs libcuefile)           |

`mpcgain` and `mpcchap` are skipped automatically if their optional
dependencies (`libreplaygain`, `libcuefile`) are not found.

Install (headers, libraries, pkg-config file, and tools):

```sh
cmake --install build
```

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

See `tests/README.md`. With `-DMPC_BUILD_TESTS=ON`, `ctest` runs five suites:
unit tests (crc32, bitstream, tables), a fixture regression that pins decode
output for bit-exactness, end-to-end integration, decoder robustness on
malformed input, and a compatibility manifest that pins the encoder to
bit-identical output with the pristine reference encoder:

```sh
cmake -S . -B build -DMPC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

## Layout

- `include/mpc/` — public headers (installed)
- `libmpcdec/` — SV7/SV8 decoder
- `libmpcenc/` — SV8 encoder core
- `libmpcpsy/` — psychoacoustic model
- `libwavformat/` — WAV read/write helper
- `common/` — shared sources (crc32, fast-math tables, tag handling)
- `tests/` — fixture generator, corpus generator, and regression harnesses
- `legacy/` — retired autotools and Visual Studio 2005 build files

## License

`libmpcdec` is BSD-licensed; `libmpcenc`, `libmpcpsy`, and the tools are
LGPL-licensed. See `LICENSE` for details.
