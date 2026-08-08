# MusicPack

MusicPack is an open, self-hosted music ecosystem built around a simple idea:
**an album should be a first-class digital object — not merely a folder full
of loosely related files.**

A specific CD, vinyl pressing, remaster or digital edition is more than a set
of tracks: it is a particular release with its own identity, artwork,
mastering, catalogue information and provenance. MusicPack keeps that identity
inside the album object itself, so a digital collection can feel like a
**digital record shelf** — intentional, curated, and collectible again. Two
editions of the same album are meaningful rather than redundant, because the
library understands why they are different.

The project deliberately gives **Musepack** a starring role. Musepack is a
mature, open and exceptionally efficient lossy codec — still one of the best
ever designed for transparent music playback — and it is the audio foundation
everything here is built on:

```text
Musepack  = the codec      (.mpc / SV7 / SV8)
MusicPack = the ecosystem  (.mpack packages, server, web client, tools)
```

The guiding rule of this repository:

> **Modernize the ecosystem aggressively; change the codec conservatively.**

## What this repository is

- **The Musepack codec (foundation)** — the decoder (`libmpcdec`), the encoder
  core (`libmpcenc`, `libmpcpsy`), a WAV helper (`libwavformat`), and the CLI
  codec tools. The decoder supports SV7/SV8; the encoder produces SV8.
- **The `.mpack` album model** — `libmusicpack` owns the package format: a
  release-group → release/edition → media → track hierarchy, assets, SHA-256
  integrity and BS.1770-5 loudness; the `musicpack` CLI authors, imports and
  verifies packages.
- **The server** — `musicpack-server` indexes an `.mpack` collection into a
  SQLite collector library and serves it over a read-only HTTP API v1 with
  **direct streaming** (the original audio bytes, never transcoded).
- **The web client** — `web/` is the first-party "digital record shelf": browse
  your collection by album and edition, then play it with Musepack
  demand-driven WASM decoding, BS.1770 album normalization, gapless album
  playback and Media Session integration.
- **WASM + demo** — the decoder builds to WebAssembly (Emscripten); `demo/` is
  a low-level playback proof-of-concept kept as a development/test artifact.

---

## The web client — a digital record shelf

`web/` (Phase 6) is the first genuinely usable MusicPack client. It is
deliberately a *digital record shelf*, not a streaming catalogue:

- sign in once with a server token (exchanged for an **HttpOnly session
  cookie**; the token is never stored in the browser)
- an artwork-first **album shelf** with infinite scrolling, search and
  "recently added"
- an **album page** that shows which exact release/edition you are looking at
  (edition chips, disc-grouped track lists, an expandable release-information
  panel with identity, provenance, codec and BS.1770 details)
- **playback** through one controller over two backends: the Musepack
  demand-driven engine (SharedArrayBuffer reader → decoder workers → an
  AudioWorklet ring) for `.mpc`, and the browser's native media stack for
  FLAC and other supported codecs
- BS.1770 album/track normalization (default −16 LUFS, true-peak capped),
  **gapless album transitions**, a queue, Media Session, and a responsive
  mobile/desktop UI

The client is a static Svelte 5 + Vite + TypeScript build served by
`musicpack-server --static-dir web/app/dist`. See `web/README.md` for the
stack decision, the `npm run dev` / `npm run build` workflow, and the
Vitest/Playwright suites.

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
musicpack-server token create --name "Web" | token list | token revoke <id>
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
- **Authentication** — `/api/v1/*` is protected by opaque bearer tokens
  (256-bit secrets; only their SHA-256 is stored), except `GET
  /api/v1/health`. Tokens are created/listed/revoked via the CLI. CORS is
  deny-by-default (`--allow-origin URL`, repeatable). The first-party web
  client exchanges a token once for an **HttpOnly session cookie**
  (`POST /api/v1/session`).
- **Live maintenance** — `POST /api/v1/library/scan` and `POST
  /api/v1/library/verify` run on single background workers (SQLite WAL), so
  playback and API reads continue and new state appears after each commit;
  `GET /api/v1/library/status` reports current/last scan + verify state.
- **HTTP API v1** — `GET /api/v1/health|albums|albums/{id}|releases/{id}|
  tracks/{id}|tracks/{id}/audio|artists|artists/{id}|assets/{id}|session`,
  with pagination, strict numeric ids, a JSON error envelope, and
  deterministic ordering. Editions are never collapsed: an album lists its
  distinct releases.
- **Direct streaming** — audio/assets are served as the **original stored
  bytes** (no decode/remux/rewrite) with full RFC 9110 single-range support
  (`206`/`416`, `Content-Range`, `Accept-Ranges`, `HEAD`), streamed from a
  file descriptor — never buffered. The bytes served hash to the manifest's
  `sha256`; a strong `ETag` (the manifest hash) enables `If-None-Match` →
  `304`. MIME and codec (`musepack-sv8`, …) are reported separately and
  derived server-side.
- **Browser streaming** — `--static-dir DIR` serves static files with
  cross-origin isolation (COOP/COEP). Point it at the built web client
  (`web/app/dist`) to get the first-party record shelf at the server root; the
  client streams Musepack through a demand-driven reader (SharedArrayBuffer +
  Atomics + a network worker with a block cache), fetching only the compressed
  ranges the decoder needs, so playback starts before the file downloads and
  seeking never fetches the whole file.

Defaults are safe: loopback binding, no remote access implied. The API spec
is `specs/musicpack-api-v1.md`.

---

## The Musepack codec (foundation)

Musepack is the audio foundation: a mature, open lossy codec whose SV7/SV8
streams the rest of the ecosystem plays and produces. The codec parts are
preserved and modernized conservatively — they are stable, well-tested, and
guarded by byte-identity regression tests.

### Building

CMake 3.16+ is required. Everything builds out of the box:

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
find_package(Musepack CONFIG REQUIRED)   # the codec library
target_link_libraries(app PRIVATE Musepack::Decoder)
```

### libmusepack decoder API

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
reader design. The decoder also builds to WebAssembly (Emscripten) and powers
the two browser front-ends above:

```sh
emcmake cmake -S . -B build-wasm && cmake --build build-wasm
```

- The **MusicPack web client** (`web/`) decodes `.mpc` over HTTP Range with a
  demand-driven reader; see `web/README.md`.
- The low-level WASM demo (`demo/`, Phase 5) is a development/test artifact
  kept for the demand-reader plumbing.

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
| `musicpack` | Author and verify `.mpack` albums (`info`, `create`, `import`, `identify`, `update-metadata`, `verify`) |
| `musicpack-server` | Index a `.mpack` library and serve it over HTTP API v1 (requires libmicrohttpd) |

## Testing

See `tests/README.md`. With `-DMPC_BUILD_TESTS=ON`, `ctest` runs twelve native
suites: unit tests (crc32, bitstream, tables), the libmusepack API suite, the
libmusicpack suite (manifest, paths, checksums, loudness meter, handoff), the
server core suite (range parser, migrations, tokens, package identity, verify,
scanner behaviors — runs on all platforms), a fixture regression that pins
decode output for bit-exactness, end-to-end integration, `.mpack` integration,
`.mpack` fuzz-lite, decoder robustness on malformed input, the server HTTP
integration (auth matrix, CORS, live scan/verify/status, streaming
byte-identity, HTTP Range, ETag/304, concurrency, security boundaries —
UNIX), and a compatibility check that pins the encoder to
bit-identical output with the pristine reference encoder (compared live
against a reference build on CI, with a committed manifest as the local
fallback). The Emscripten build registers an additional `wasm_smoke` suite
(including the demand-driven range-reader path: PCM identity, a seek-to-90%
fetch-accounting check, and network-failure injection) and the
`web_wasm_gapless` suite (the web client's audio guarantees: gapless
two-track continuity, exact track-end frames, demand-reader seek accounting).

The web client adds its own suites (`web/`): Vitest units for the controller,
queue, ring buffer, loudness math, API client and session; a Playwright
browser suite (authenticate → shelf → album → edition switch → Musepack and
native playback → seek → gapless → queue → Media Session → 401 re-auth) run
in the CI `web-client` job; and a performance report script. See
`web/README.md`.

```sh
cmake -S . -B build -DMPC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

## Layout

- `libmpcdec/` — SV7/SV8 decoder + `musepack_decoder` facade (the codec)
- `libmpcenc/` — SV8 encoder core
- `libmpcpsy/` — psychoacoustic model
- `libwavformat/` — WAV read/write helper
- `include/musepack/` — stable `libmusepack` public API
- `include/mpc/` — historical public headers (installed for compatibility)
- `libmusicpack/` — `.mpack` package library (`libmusicpack`)
- `musicpack/` — `musicpack` CLI
- `server/` — `musicpack-server`: scanner, SQLite collector library, HTTP API v1, direct streaming (vendored SQLite in `server/vendor/`)
- `web/` — the Phase 6 web client (Svelte 5 + Vite + TS): the digital record shelf
- `wasm/` — Emscripten build of the decoder + WASM wrapper + smoke test
- `demo/` — low-level browser playback proof-of-concept
- `specs/` — `.mpack` v1 spec + JSON Schema, and the server API spec (`musicpack-api-v1.md`)
- `common/` — shared sources (crc32, fast-math tables, tag handling)
- `tests/` — fixture generator, corpus generator, and regression harnesses
- `legacy/` — retired autotools and Visual Studio 2005 build files

## License

The codec layer: `libmpcdec` is BSD-licensed; `libmpcenc`, `libmpcpsy`, and the
codec tools are LGPL-licensed. The MusicPack layer (`libmusicpack`, the
`musicpack` CLI, and `musicpack-server`) is BSD 3-clause licensed. See
`LICENSE` for details.
