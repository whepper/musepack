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
| `MPC_BUILD_SERVER`   | `ON`                | Build `musicpack-server` (needs libmicrohttpd)|

`mpcgain` and `mpcchap` are skipped automatically if their optional
dependencies (`libreplaygain`, `libcuefile`) are not found.
`musicpack-server` is skipped when `libmicrohttpd` is unavailable (Windows is
not supported for the server executable; the server core still builds there).

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

## libmusicpack — the `.mpack` package model

`libmusicpack` owns MusicPack package semantics: the `.mpack` v1 manifest,
album/release-group → release/edition → media → track model, assets, SHA-256
integrity, BS.1770-5 loudness (measured per track and as an album program)
and directory-bundle storage. It depends on `libmusepack` (for the Musepack
track handoff) but never the reverse. It builds as `libmusicpack`, exported
as `MusicPack::Package`:

```cmake
find_package(MusicPack CONFIG REQUIRED)
target_link_libraries(app PRIVATE MusicPack::Package)
```

```c
#include <musicpack/musicpack.h>

musicpack_package *pkg = musicpack_package_open_dir("Album.mpack", 0);
const musicpack_manifest *m = musicpack_package_manifest(pkg);
musicpack_report rep;
musicpack_package_verify(pkg, &rep, 0, 0);

/* Musepack handoff: expose a track's .mpc to the codec layer */
mpc_reader reader;
musicpack_package_track_open_reader(pkg, 0, 0, &reader);
musepack_decoder *d = musepack_decoder_open(&reader, 0);
```

The `musicpack` CLI (`musicpack info|verify|identify|create|import|update-metadata`)
builds, inspects and validates directory-form packages. `info` shows collector
identity (release type, edition, release/original dates, country, label,
catalogue number, medium, barcode); `create`/`import` accept release options.
`import` reads embedded metadata (Vorbis Comments from FLAC, APEv2 from
`.mpc`) to fill album/track/release/identifier/source fields — explicit flags
override tags, and it never invents edition/country/label/catalogue/type from
filenames. `identify` matches a package against MusicBrainz (exact release ID
lookup, or barcode search via `curl`; `--mb-json` applies an offline release
document) and enriches empty fields with honest `identity.confidence`
(`exact`/`confirmed`/`probable`/`none`) — importing never requires a network.
`update-metadata` reconciles the manifest from the tracks' tags and, with
`--sync-tags`, writes the manifest back into `.mpc` APEv2 tags and refreshes
checksums (unknown fields preserved throughout). The normative spec and
machine-readable schema live in `specs/musicpack-v1.md` and
`specs/musicpack-v1.schema.json`; committed reference packages (a Musepack
album and a FLAC album) are under `tests/reference/`.

## musicpack-server — self-hosted library server

`musicpack-server` (in `server/`) indexes a real `.mpack` collection into a
SQLite collector library and serves it over a read-only HTTP API v1. It
consumes `libmusicpack` for all package parsing, validation and path
security; it never parses manifests itself.

```sh
musicpack-server scan    --library ~/Music [--database library.db] [--verify]
musicpack-server serve   --library ~/Music [--listen 127.0.0.1] [--port 8080]
musicpack-server verify  --library ~/Music
```

- **Scanner** — deterministic and idempotent; finds `.mpack` bundles (recursion
  stops at a package root), validates through libmusicpack, and ingests each
  package in its own transaction. A package's manifest sha256 gives cheap
  change detection; a content fingerprint keeps a *moved* package the same
  album. Removed packages become `unavailable`; malformed packages are
  recorded `invalid` without touching the index. Scan policy defaults to
  manifest + object existence; `--verify` runs full SHA-256 verification.
- **Collector library** — SQLite (vendored amalgamation) preserving the
  release-group → release/edition → media → track hierarchy, with artists,
  assets, package status, and schema migrations.
- **HTTP API v1** — `GET /api/v1/health|albums|albums/{id}|releases/{id}|
  tracks/{id}|tracks/{id}/audio|artists|artists/{id}|assets/{id}`, with
  pagination, strict numeric ids, a JSON error envelope, and deterministic
  ordering. Editions are never collapsed: an album lists its distinct
  releases.
- **Direct streaming** — audio/assets are served as the **original stored
  bytes** (no decode/remux/rewrite) with full RFC 7233 single-range support
  (`206`/`416`, `Content-Range`, `Accept-Ranges`, `HEAD`), streamed from a
  file descriptor — never buffered. The bytes served hash to the manifest's
  `sha256`. MIME and codec (`musepack-sv8`, …) are reported separately and
  derived server-side.

Defaults are safe: loopback binding, no remote access implied. The API spec
is `specs/musicpack-api-v1.md`. The browser demo can play a server track over
HTTP Range through the existing WASM decoder (`demo/README.md`).

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
| `musicpack-server` | Index a `.mpack` library and serve it over HTTP API v1 (requires libmicrohttpd) |

## Testing

See `tests/README.md`. With `-DMPC_BUILD_TESTS=ON`, `ctest` runs twelve native
suites: unit tests (crc32, bitstream, tables), the libmusepack API suite, the
libmusicpack suite (manifest, paths, checksums, loudness meter, handoff), the
server core suite (range parser, migrations, package identity, scanner
behaviors — runs on all platforms), a fixture regression that pins decode
output for bit-exactness, end-to-end integration, `.mpack` integration,
`.mpack` fuzz-lite, decoder robustness on malformed input, the server HTTP
integration (API, streaming byte-identity, HTTP Range, concurrency, security
boundaries — UNIX), and a compatibility check that pins the encoder to
bit-identical output with the pristine reference encoder (compared live
against a reference build on CI, with a committed manifest as the local
fallback). The Emscripten build registers an additional `wasm_smoke` suite
(including the JS range-reader decode path):

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
- `libmusicpack/` — `.mpack` package library (`libmusicpack`)
- `musicpack/` — `musicpack` CLI
- `server/` — `musicpack-server`: scanner, SQLite collector library, HTTP API v1, direct streaming (vendored SQLite in `server/vendor/`)
- `wasm/` — Emscripten build of the decoder + WASM wrapper + smoke test
- `demo/` — browser playback proof-of-concept
- `specs/` — `.mpack` v1 spec + JSON Schema, and the server API spec (`musicpack-api-v1.md`)
- `common/` — shared sources (crc32, fast-math tables, tag handling)
- `tests/` — fixture generator, corpus generator, and regression harnesses
- `legacy/` — retired autotools and Visual Studio 2005 build files

## License

`libmpcdec` is BSD-licensed; `libmpcenc`, `libmpcpsy`, and the tools are
LGPL-licensed. See `LICENSE` for details.
