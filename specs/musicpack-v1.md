# MusicPack `.mpack` v1 — manifest and directory bundle

Status: **normative for the v1 freeze**. Version 1.

MusicPack treats a **specific release or edition as a collectible digital
object**: a package describes exactly one release/edition of an album, and
multiple versions of the same album are intentionally meaningful, distinct
objects (an original CD, a remaster, a digital deluxe edition, ...). A package
must be able to say *which* release it holds without relying on filenames,
folder names, or an external MusicBrainz lookup.

## 1. Scope

`.mpack` is the MusicPack album/release package format. Phase 2 defines the
**directory bundle** representation. A future packed single-file `MPAK`
representation will be a different *storage backend* for the same logical
model; this specification deliberately contains no offsets, packing, or
container details.

```text
manifest  = what the album IS      (logical model, storage-independent)
storage   = how the album is STORED (directory bundle in Phase 2)
```

The format identifier is `"musicpack"` (manifest field `format`) and the
schema version is `1` (manifest field `version`). Because v1 is not yet
released, these additions are part of version 1; there is no v2.

### Logical hierarchy

```text
album / release group        ("album" object: title, artists, type, genres)
        ↓
specific release / edition   ("release" object: edition, dates, label, country, ...)
        ↓
media                        ("media[]": one entry per disc/medium, each with a format)
        ↓
tracks                       ("media[].tracks[]")
        ↓
audio objects                ("track.audio": the actual file)
```

Orthogonal metadata (not part of the hierarchy, referencing it as needed):
`identifiers`, `identity`, `source`, `provenance`, `artwork`/`booklet`/
`lyrics`/`extras`, `loudness`, integrity (`sha256`).

Each field belongs to exactly one level; no field mixes two levels. In
particular:

- **release-group level** (`album`): `title`, `artists`, `releaseType`,
  `originalReleaseDate`, `genres`.
- **specific-release level** (`release`): `releaseDate`, `edition`,
  `country`, `label`, `catalogueNumber`, `notes`.
- **medium level** (`media[]`): `disc`, `format`, `title`.
- `release` (the specific release) is never merged into `source` (where the
  audio came from); the two are independent (see §4).

## 2. Directory bundle layout

```text
Album.mpack/
├── manifest.json        # the manifest (normative)
├── audio/               # audio objects (one per track)
├── artwork/             # artwork objects (role-tagged)
├── booklet/             # booklet documents
├── lyrics/              # lyrics documents
└── extras/              # anything else the author wants
```

- The manifest is the **authority**: files it references define the package.
- Files present on disk but not referenced are *extra files*; validation
  reports them as warnings, never errors, so ordinary files remain usable
  outside MusicPack (no archive/extraction required).
- `manifest.json` is **not** self-hashed (avoiding a self-referential
  checksum); integrity is per-asset `sha256`.
- All object paths are relative to the package root and use `/` separators.

### Path rules (canonical)

Rejected at parse time:

- absolute paths (leading `/`, drive letters, UNC, URL schemes);
- `..` and `.` segments, empty segments, trailing `/`, empty paths;
- backslash `\`, `:`, and control characters (NUL, < 0x20, 0x7f);
- paths longer than 4096 characters.

Containment: any resolution of a manifest path is verified against the
package root with `realpath` (POSIX) / `GetFullPathName` (Windows); symlinks
that resolve outside the root are rejected.

## 3. Manifest schema

Machine-readable validation: `specs/musicpack-v1.schema.json`.

Field summary (see the JSON Schema for full constraints):

| Field                  | Required | Notes                                        |
|------------------------|----------|----------------------------------------------|
| `format`               | yes      | literal `"musicpack"`                        |
| `version`              | yes      | `1`; unknown majors rejected cleanly         |
| `album`                | yes      | `title` + non-empty `artists[]`; optional `releaseType`, `originalReleaseDate`, `genres[]` |
| `release`              | no       | the specific release/edition: `releaseDate`, `edition`, `country`, `label`, `catalogueNumber`, `notes` |
| `identifiers`          | no       | `musicbrainzReleaseGroupId` (group level), `musicbrainzReleaseId` (release level), `barcode` |
| `identity`             | no       | `source` + `confidence` describing how IDs matched |
| `source`               | no       | `type` (`cd-rip`, `digital-download`, ...), `store`, `sourceId` |
| `media`                | yes      | non-empty array of media; each has `disc` (>=1), optional `format` (closed enum), `tracks[]` |
| track fields           | yes/var  | `track` (>=1), `title`, `audio`; optional `artists`, `identifiers` (`isrc`, `musicbrainzTrackId`, `musicbrainzRecordingId`), `source`, `sourceAudio`, `duration` (derived), `loudness` |
| `audio`                | yes      | object: `path` (required), `sha256` (required, 64 lowercase hex), `codec` (optional) |
| `artwork`              | no       | array of `{ role, path, sha256 }`             |
| `booklet`,`lyrics`,`extras` | no | arrays of `{ path, sha256? }`             |
| `loudness`             | no       | album-level `algorithm`, `albumLUFS`, `albumTruePeakDbTP` |
| `provenance`           | no       | `tool`, `toolVersion`; timestamps omitted by default for determinism |

Disc numbers are unique; track numbers are unique within a disc; object paths
are unique across the whole package.

### Release type

`album.releaseType` is a **closed enumeration** — it is never inferred from
track count. Unknown values are rejected at parse time; `other` is the
escape hatch for anything not listed:

```text
album
ep
single
maxi-single
compilation
soundtrack
live-album
remix-album
box-set
other
```

### Medium format

`media[].format` is a **closed enumeration** describing the physical or
digital medium. `release.edition` (which *edition* this is) and `format`
(which *medium*) are deliberately separate fields and must not be collapsed:

```text
CD
SACD
Vinyl
Cassette
Digital
Blu-ray Audio
DVD-Audio
Other
```

A two-disc CD edition is represented as two `media[]` entries, both with
`format: "CD"`, belonging to one `release`. A digital release typically has a
single logical medium with `format: "Digital"` (and `disc: 1`).

### Track audio object

```json
"audio": {
  "path": "audio/01 - First Track.mpc",
  "sha256": "fffecfe0220e73b4056279ed978f40485e0afcadd56f9e2a63d74111fbee4240",
  "codec": "musepack-sv8"
}
```

`path` is the only linkage to storage; `codec` is informational and optional.

## 4. Identity vs provenance

Distinct concepts, never merged:

- **`identifiers`** — durable IDs. Release-group identity
  (`musicbrainzReleaseGroupId`) is distinguishable from specific-release
  identity (`musicbrainzReleaseId`); per-track `isrc`,
  `musicbrainzTrackId`, `musicbrainzRecordingId`. MusicBrainz IDs **enhance**
  identity; they are never required for a package to represent a release.
- **`identity`** — how the IDs were matched:
  - `source`: `musicbrainz` | `store` | `local`
  - `confidence`: `exact` | `confirmed` | `probable` | `none`
  Fuzzy matches are recorded as `probable` or `local`; they are **never**
  silently promoted to authoritative identity.
- **`release`** — *which* specific release/edition this package is. Never
  populated from `source`; do not encode edition in `source`.
- **`source` / per-track `source`** — *where the audio came from* (e.g.
  `{ "type": "digital-download", "store": "Deezer", "sourceId": "..." }`,
  `{ "type": "cd-rip" }`; per-track `{ "store": "Deezer", "trackId": "..." }`).
- **`provenance`** — how the package itself was built (`tool`, `toolVersion`).
- **`sourceAudio`** (per-track, optional) — the pre-encoding source file
  (`{ "codec": "flac", "md5": "..." }`), for tracing without implying identity.

## 5. Loudness (BS.1770 only)

Canonical `.mpack` loudness is measured with **ITU-R BS.1770-5** (the
manifest may record the revision in `loudness.algorithm`; the canonical value
is `MUSICPACK_LOUDNESS_STANDARD` = `"ITU-R BS.1770-5"`). Classic ReplayGain
read from `.mpc` is import-time compatibility data and is never canonical
MusicPack loudness.

What MusicPack implements (BS.1770-5):

- **Integrated loudness** (`trackLUFS`, `albumLUFS`): K-weighted, 400 ms
  blocks, with the standard absolute gate (−70 LU) and relative gate
  (−10 LU). For the mono/stereo channel counts MusicPack meters, the
  algorithm is identical to BS.1770-4.
- **True peak** (`truePeakDbTP`, `albumTruePeakDbTP`): 4×-oversampled
  (49-tap sinc) interpolated peak, in dBTP. Values below −70 dBTP are
  floored at −70.
- **Not stored**: loudness range (LRA) and channel configurations above
  stereo (BS.1770-5's multichannel additions).

Rules:

- Stored per track: `trackLUFS` (integrated) and `truePeakDbTP` (per-track
  program).
- Stored per album (package): `albumLUFS` and `albumTruePeakDbTP`.
- **Album loudness is measured as an album, not aggregated.** All tracks are
  fed in track order through a single meter as one concatenated program; the
  gating blocks run continuously across track boundaries (no per-track state
  reset, which is the standard BS.1770 program-loudness semantics). This is
  never an arithmetic mean of track LUFS, a duration-weighted approximation,
  or derived from stored track values. Track loudness remains measured
  independently, each with its own meter.
- **Album true peak is the maximum relevant true peak across the album
  program** (the max over all tracks), in dBTP.
- **Gain is derived, not stored**: `gain_db = target_lufs - measured_lufs`.
  The library provides `musicpack_loudness_compute_gain()`. No playback
  target is hard-coded by the format.
- Measured loudness is separate from any future playback-policy target.

## 6. Integrity

SHA-256 (lowercase hex) per audio/artwork/booklet/lyrics asset.
`musicpack verify` detects: missing files, checksum mismatches, malformed
manifests, duplicate track identity, invalid paths, impossible numbering,
invalid loudness values, invalid release-type / medium-format enumeration
values, unsupported manifest version, and (as warnings) unreferenced files.

## 7. Validation / forward compatibility

- Unknown fields are **ignored on read** and **preserved on write**: a
  read-modify-write round-trip keeps extension data (patch-on-original),
  including inside `album`, `release`, `identifiers` and `media`.
- Unknown schema majors are rejected cleanly (`version` != 1).
- New fields must be added as optional fields; existing fields must not
  change meaning. `releaseType` and `media[].format` are closed enums by
  design; new values are added to both the schema and the parser in a
  future revision.

## 8. Security model

`.mpack` is untrusted input. The library enforces the path rules above,
bounds manifest size (16 MiB), bounds JSON nesting (100), validates checksum
format and loudness ranges, and treats `extras/` as opaque data (never
executed or interpreted). Fuzzing targets cover manifest parsing, path
normalization and package validation.

## 9. Collector examples

Different editions of the same album are distinct, meaningful packages:

### Example Album — 1987 European CD (original mastering)

```json
{
  "format": "musicpack",
  "version": 1,
  "album": {
    "title": "Example Album",
    "artists": [ { "name": "Example Artist", "role": "main" } ],
    "releaseType": "album",
    "originalReleaseDate": "1986-06-16"
  },
  "release": {
    "releaseDate": "1987-01-01",
    "edition": "1987 European CD",
    "country": "DE",
    "label": "Example Records",
    "catalogueNumber": "EXA 1987"
  },
  "identifiers": { "musicbrainzReleaseId": "...", "barcode": "0198765432197" },
  "identity": { "source": "local", "confidence": "none" },
  "source": { "type": "cd-rip" },
  "media": [ { "disc": 1, "format": "CD", "tracks": [ { "track": 1, "title": "...",
    "audio": { "path": "audio/01.mpc", "sha256": "..." } } ] } ]
}
```

### Example Album — 2001 Remaster (CD)

```json
{
  "format": "musicpack",
  "version": 1,
  "album": {
    "title": "Example Album",
    "artists": [ { "name": "Example Artist", "role": "main" } ],
    "releaseType": "album",
    "originalReleaseDate": "1986-06-16"
  },
  "release": {
    "releaseDate": "2001-09-14",
    "edition": "2001 Remaster",
    "country": "GB",
    "label": "Example Records",
    "catalogueNumber": "EXA 2001"
  },
  "identifiers": {
    "musicbrainzReleaseGroupId": "rg-...",
    "musicbrainzReleaseId": "rel-2001-..."
  },
  "identity": { "source": "musicbrainz", "confidence": "exact" },
  "source": { "type": "cd-rip" },
  "media": [ { "disc": 1, "format": "CD", "tracks": [ { "track": 1,
    "identifiers": { "isrc": "...", "musicbrainzTrackId": "...", "musicbrainzRecordingId": "..." },
    "title": "...", "audio": { "path": "audio/01.mpc", "sha256": "..." } } ] } ]
}
```

### Example Album — 2016 Digital Deluxe Edition

```json
{
  "format": "musicpack",
  "version": 1,
  "album": {
    "title": "Example Album",
    "artists": [ { "name": "Example Artist", "role": "main" } ],
    "releaseType": "album",
    "originalReleaseDate": "1986-06-16"
  },
  "release": {
    "releaseDate": "2016-09-23",
    "edition": "2016 Digital Deluxe Edition",
    "country": "XE",
    "label": "Example Records",
    "catalogueNumber": "EXA 2016D"
  },
  "source": { "type": "digital-download", "store": "Deezer" },
  "media": [ { "disc": 1, "format": "Digital",
    "tracks": [ { "track": 1, "title": "...", "audio": { "path": "audio/01.mpc", "sha256": "..." } } ] } ]
}
```

### Example Single — 1992 CD Maxi-Single

```json
{
  "format": "musicpack",
  "version": 1,
  "album": {
    "title": "Example Single",
    "artists": [ { "name": "Example Artist", "role": "main" } ],
    "releaseType": "maxi-single",
    "originalReleaseDate": "1992-05-01"
  },
  "release": {
    "releaseDate": "1992-05-01",
    "edition": "1992 CD Maxi-Single",
    "country": "GB",
    "label": "Example Records",
    "catalogueNumber": "EXA 1992M"
  },
  "media": [ { "disc": 1, "format": "CD", "tracks": [ { "track": 1, "title": "...",
    "audio": { "path": "audio/01.mpc", "sha256": "..." } } ] } ]
}
```

### Example EP — Digital

```json
{
  "format": "musicpack",
  "version": 1,
  "album": {
    "title": "Example EP",
    "artists": [ { "name": "Example Artist", "role": "main" } ],
    "releaseType": "ep",
    "originalReleaseDate": "2020-04-17"
  },
  "release": { "releaseDate": "2020-04-17", "edition": "2020 Digital", "country": "US" },
  "media": [ { "disc": 1, "format": "Digital", "tracks": [ { "track": 1, "title": "...",
    "audio": { "path": "audio/01.mpc", "sha256": "..." } } ] } ]
}
```

The three `Example Album` packages share the same release group (same
`album.title` and, when known, the same `musicbrainzReleaseGroupId`) but are
distinct collectible objects with different editions, dates, mediums,
catalogue numbers, identifiers and provenance.
