# MusicPack web client — the digital record shelf

The first-party MusicPack web application (Phase 6). It is a **digital record
shelf**, not a streaming clone: the primary entity is an intentionally
collected album/release, multiple editions are meaningful (never flattened),
and technical release information sits alongside the artwork.

```text
authenticate (token once) → HttpOnly session cookie
    ↓
album shelf (artwork-first grid, infinite scroll, search, recently added)
    ↓
album page → edition selector → disc-grouped track list → release info panel
    ↓
Play Album → BS.1770 album normalization → Musepack demand-driven WASM
             decoding → AudioWorklet ring → gapless to the next track
```

## Technology decision

| choice        | reason                                                                 |
|---------------|------------------------------------------------------------------------|
| **Svelte 5**  | Smallest shipped JS of the component frameworks (compiles away, no VDOM runtime); declarative components + built-in a11y linting keep a long-lived first-party client small and maintainable. |
| **Vite**      | First-class Svelte/TS support, fast dev server, static build with no SSR, emits the AudioWorklet as a bundled entry. |
| **TypeScript**| The codebase and the API contract are typed; strict mode + `noUncheckedIndexedAccess`. |
| **Vitest**    | Node unit tests for the framework-free core (controller, queue, ring, loudness, API client). |
| **Playwright**| Real Chromium browser integration against the actual server + wasm decoder. |

The client is plain static assets (`npm run build` → `web/app/dist`) served by
`musicpack-server --static-dir`. There is no Node runtime in production.

## Layout

```text
web/
  app/                 Vite root (the application)
    public/            classic worker + wasm + demand-reader scripts (synced)
    src/
      lib/
        api/           typed HTTP v1 client + error mapping
        auth/          session store (token → cookie, never stored)
        state/         library (shelf, editions), queue, player model
        playback/      controller, MusepackEngine, NativeBackend, RingBuffer,
                       AudioWorklet, loudness, Media Session
        ui/            Svelte components + theme
        router.ts      history-API SPA router
  tests/
    unit/              Vitest units + the wasm+ring feed test
    node/              Node wasm gapless/seek harness (ctest web_wasm_gapless)
    e2e/               Playwright browser suite
    perf/              perf report script
  scripts/sync-wasm.sh copies the built wasm + demand reader into app/public
```

## Development

Requirements: a built `libmusepack.wasm` module (`build-wasm/wasm/musepack.*`)
and a running `musicpack-server` with a scanned library.

```sh
# 1. build the wasm decoder module (once)
emcmake cmake -S . -B build-wasm && cmake --build build-wasm --target musepack_wasm -j

# 2. build the server (once), scan a library, create a token
cmake -S . -B build && cmake --build build -j --target musicpack_server_cmd
build/server/musicpack-server scan --library ./library --database ./library.db
build/server/musicpack-server token create --name Web --database ./library.db

# 3. run the client against the server (Vite proxies /api, COOP/COEP on)
cd web && npm install && npm run dev        # http://localhost:5173

# production build + serve
cd web && npm run build
../build/server/musicpack-server serve --library ./library --database ./library.db \
    --static-dir web/app/dist
```

**Cross-origin isolation:** the demand-driven reader needs SharedArrayBuffer,
so the page must be cross-origin isolated. `musicpack-server --static-dir`
already sends `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`; the Vite dev server sends the
same headers. The server must be reachable at `http://127.0.0.1:8080` for the
dev proxy (or set `server.proxy` in `vite.config.ts`).

**Authentication:** sign in with a server token once. It is exchanged for an
HttpOnly `musicpack_session` cookie and is never stored in the browser.
Bearer tokens remain supported by the API for CLI/native clients.

## Tests

```sh
cd web
npm run check          # svelte-check + tsc
npm run test:unit      # Vitest units (needs build-wasm for the wasm+ring test)
npm run test:node      # Node wasm gapless/seek harness
npx playwright test    # browser e2e (builds a fixture library + starts the server)
node tests/perf/perf.mjs   # performance report
```

The `web_wasm_gapless` ctest (registered under the wasm build) runs the Node
harness in CI.

## Gapless and native playback notes

- **Musepack (exact):** two decoder workers — the current track's worker and a
  second worker already opened on the next track. At the exact sample boundary
  the controller promotes the standby worker and keeps feeding the same ring,
  so adjacent tracks of a release play without inserted silence.
- **Native codecs (browser `<audio>`, e.g. FLAC):** the next track is
  preloaded into a second element and swapped in on `ended`. Browsers expose
  no sample-perfect gapless API for `<audio>`, so a small boundary gap may
  occur; this is a platform limitation, not a codec defect.

## BS.1770 loudness normalization

Client playback policy (the `.mpack` values are never modified):

- Off / Album / Track, default **Album**.
- Target **−16 LUFS**; gain = target − measured, capped so the output true
  peak never exceeds **−1 dBTP**.
- Output gain = user volume × normalization gain (kept separate, combined in
  the linear domain in one `GainNode`).
