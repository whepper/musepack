# MusicPack HTTP API v1

This document is the human-readable specification for the MusicPack server's
read-only HTTP API (`musicpack-server`). API versioning is **independent** of
`.mpack` manifest versioning (`specs/musicpack-v1.md`).

Status: **v1** (Phase 4). Read-only, collector-oriented, local/trusted-network.

## 1. Conventions

### Base path

All endpoints are under `/api/v1/`. The version prefix changes only on
incompatible API changes; additive extensions never change it.

### Identifiers

Resource ids are decimal integers (SQLite rowids) carried as JSON numbers.
Client-supplied ids are parsed **strictly**: only ASCII digits, at most 18
characters, value ≤ 2^63−1. Anything else is a `400 invalid_request`.

### Methods

`GET` and `HEAD` only. Anything else is `405 unsupported_method`.

### Pagination

List endpoints support:

```
?limit=&offset=
```

- `limit` default 50, clamped to `1..200`
- `offset` default 0, clamped to `0..100000`
- responses include `limit`, `offset`, and `total`

List ordering is deterministic:

- `albums`: album artist (first artist), then title (case-insensitive), then
  original release date
- `artists`: name (case-insensitive)
- nested `releases` within an album: release date, then id

### Errors

Every failure returns a consistent JSON envelope:

```json
{ "error": { "code": "not_found", "message": "Track not found" } }
```

| code                | HTTP  | meaning                                   |
|---------------------|-------|-------------------------------------------|
| `invalid_request`   | 400   | malformed id, pagination, or path         |
| `not_found`         | 404   | unknown resource or endpoint              |
| `unsupported_method`| 405   | non-GET/HEAD                              |
| `bad_range`         | 416   | unsatisfiable or malformed `Range`        |
| `unavailable`       | 503   | package/file missing or invalid           |
| `internal`          | 500   | server-side failure                       |

Raw SQLite/parser errors are never exposed to clients. Filesystem paths are
never returned in responses.

## 2. Resources

### `GET /api/v1/health`

```json
{
  "status": "ok",
  "version": "0.1.0",
  "apiVersion": "v1",
  "schemaVersion": 1
}
```

### `GET /api/v1/albums`

List of release groups (albums). Collector model: editions are never merged.

```json
{
  "albums": [
    {
      "id": 2,
      "title": "Example Album",
      "releaseType": "album",
      "originalReleaseDate": "1986-06-16",
      "artists": [ { "id": 1, "name": "Artist", "role": "main" } ],
      "releaseCount": 2
    }
  ],
  "limit": 50, "offset": 0, "total": 1
}
```

### `GET /api/v1/albums/{id}`

```json
{
  "album": {
    "id": 2, "title": "Example Album", "releaseType": "album",
    "originalReleaseDate": "1986-06-16",
    "artists": [ { "id": 1, "name": "Artist" } ]
  },
  "releases": [
    {
      "id": 10, "edition": "Original European CD", "releaseDate": "1986-06-16",
      "country": "DE", "label": "X", "catalogueNumber": "Y", "barcode": "…",
      "media": ["CD"], "trackCount": 12,
      "packageStatus": "valid", "verifyStatus": "unverified"
    }
  ]
}
```

`releases[]` shows the distinct editions; a client renders
`Album └── N versions`. `media` is the list of distinct medium formats
(`["CD"]`, `["CD", "Digital"]`, `["Digital"]`).

### `GET /api/v1/releases/{id}`

Full release/edition detail. Top-level fields mirror `.mpack` v1's
`release` block (edition, releaseDate, country, label, catalogueNumber,
barcode, notes, mbid, identity/source/provenance) plus `packageStatus` /
`verifyStatus`. `album` carries the release-group summary and artists.

```json
{
  "id": 10,
  "edition": "2016 Remaster",
  "releaseDate": "2016-09-23",
  "album": { "id": 2, "title": "Example Album", "artists": [ ... ] },
  "media": [
    {
      "disc": 1, "format": "CD",
      "tracks": [
        {
          "id": 55, "number": 1, "title": "Track",
          "artists": [ { "id": 1, "name": "Artist" } ],
          "isrc": "…",
          "loudness": { "lufs": -7.19, "truePeakDb": -4.18 },
          "codec": {
            "codec": "musepack-sv8", "mimeType": "audio/musepack",
            "streamVersion": 8, "sampleRate": 44100, "channels": 2
          },
          "audio": { "id": 90, "size": 28288, "sha256": "…",
                     "url": "/api/v1/tracks/55/audio" }
        }
      ]
    }
  ],
  "artwork": [ { "id": 7, "kind": "artwork", "role": "front",
                 "mimeType": "image/jpeg",
                 "url": "/api/v1/assets/7" } ],
  "assets": [ { "id": 8, "kind": "booklet", "mimeType": "application/pdf",
                "url": "/api/v1/assets/8" } ],
  "packageStatus": "valid", "verifyStatus": "unverified"
}
```

### `GET /api/v1/tracks/{id}`

Track detail with a small `context` block (album/release/disc).

### `GET /api/v1/tracks/{id}/audio`

Direct byte serving of the original contained audio object. See §3.

### `GET /api/v1/assets/{id}`

Controlled asset serving. Only artwork/booklet/lyrics assets are served
(`extras` are indexed but never exposed here). Asset paths always come from
the validated package model + containment resolution — callers can never
supply a filesystem path.

### `GET /api/v1/artists` / `GET /api/v1/artists/{id}`

Artist list (with `albumCount`) and artist detail (with `albums[]`).

## 3. Direct streaming + HTTP Range

`/tracks/{id}/audio` and `/assets/{id}` serve the **original stored bytes**:
no decode, remux, re-encode, normalization or tag rewriting. Files are
streamed from an fd; never loaded into RAM.

- `200` full file, `Content-Length: <size>`, `Accept-Ranges: bytes`,
  `Content-Type: <mime>`
- `206 Partial Content` for a satisfiable single range
- `416 Range Not Satisfiable` for unsatisfiable or malformed ranges, with
  `Content-Range: bytes */<size>`

Single range syntax (RFC 7233), all supported:

```
Range: bytes=0-1023      first 1024 bytes
Range: bytes=1024-       open-ended
Range: bytes=-4096       last 4096 bytes (suffix)
```

Semantics:

- `bytes=0-0` on a non-empty file → 1 byte (206)
- an end beyond EOF is clamped to the file end
- a first-byte-pos ≥ size → 416
- `bytes=-N` with N > size → whole file (206)
- `bytes=-0`, non-`bytes` units, multiple ranges, or non-numeric bounds → 416
- range lengths/offsets are 64-bit

Responses:

```
206 Partial Content
Content-Range: bytes 0-1023/28288
Content-Length: 1024
Accept-Ranges: bytes
Content-Type: audio/musepack
```

`HEAD` on audio/assets returns headers only (correct `Content-Length`,
`Accept-Ranges`, `Content-Type`), no body.

**Integrity guarantee:** the bytes served for a track hash to the `sha256`
recorded for that audio object in the `.mpack` manifest.

## 4. MIME / codec semantics

MIME type is presentation; the codec string is the application's identity for
a stream. Both are derived server-side from the object path/stream — never
from manifest claims.

| extension | MIME                    | codec            |
|-----------|-------------------------|------------------|
| `.mpc`    | `audio/musepack`        | `musepack-sv8` / `musepack-sv7` (probed) |
| `.flac`   | `audio/flac`            | `flac`           |
| `.wav`    | `audio/wav`             | `wav`            |
| `.ogg`    | `audio/ogg`             | `vorbis`         |
| `.jpg/.jpeg/.png/.gif/.webp/.bmp` | `image/*` | — |
| `.pdf`    | `application/pdf`       | —                |
| `.lrc/.txt/.md` | `text/plain`      | —                |
| other     | `application/octet-stream` | `unknown`     |

`sampleRate`, `channels`, and `streamVersion` are probed from headers at scan
time (libmusepack for Musepack; FLAC STREAMINFO). `duration` comes from the
manifest (derived, not canonical).

## 5. Versioning rules

- The URL prefix (`/api/v1/`) is the API version; bump on incompatible change.
- Fields are added, never removed/renamed, within a version.
- The `.mpack` manifest schema version (`specs/musicpack-v1.md`) is
  independent of the API version.
- A server may support multiple API versions concurrently.

## 6. Concurrency

- The server runs a single MHD event-loop thread; request handlers need no
  locks. SQLite is WAL mode with a 5 s busy timeout.
- Concurrent streams and API reads are safe; each request has independent
  file/object state.
- `musicpack-server scan` may run while the server is serving; WAL allows the
  reader to pick up committed changes. In-process rescan is intentionally not
  implemented in Phase 4.

## 7. Security posture

- Loopback binding by default; never auto-exposes remote access.
- No arbitrary-path endpoints; serving only from DB ids + containment-checked
  package paths (realpath, no `..`, no symlink escape).
- Strict numeric parsing for ids and ranges; bounded paths/pagination.
- No shell execution in request handling.
- `extras/` is indexed but not served.
- Phase 4 is local/trusted-network development; authentication is deferred to
  a later phase.
