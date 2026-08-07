# MusicPack `.mpack` v1 — manifest and directory bundle

Status: **normative for Phase 2**. Version 1.

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
schema version is `1` (manifest field `version`).

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

| Field              | Required | Notes                                             |
|--------------------|----------|---------------------------------------------------|
| `format`           | yes      | literal `"musicpack"`                             |
| `version`          | yes      | `1`; unknown majors rejected cleanly              |
| `album`            | yes      | `title` + non-empty `artists[]`; optional `releaseDate`, `genres[]` |
| `identifiers`      | no       | `musicbrainzReleaseId`, `barcode` (durable IDs)   |
| `identity`         | no       | `source` + `confidence` describing how IDs matched|
| `source`           | no       | `type` (`cd-rip`, `digital-download`, ...), `store`, `sourceId` |
| `media`            | yes      | non-empty array of discs; each has `disc` (>=1), `tracks[]` |
| track fields       | yes/var  | `track` (>=1), `title`, `audio`; optional `artists`, `identifiers.isrc`, `source`, `sourceAudio`, `duration` (derived), `loudness` |
| `audio`            | yes      | object: `path` (required), `sha256` (required, 64 lowercase hex), `codec` (optional) |
| `artwork`          | no       | array of `{ role, path, sha256 }`                 |
| `booklet`,`lyrics`,`extras` | no | arrays of `{ path, sha256? }`               |
| `loudness`         | no       | album-level `albumLUFS`, `albumTruePeakDbTP`      |
| `provenance`       | no       | `tool`, `toolVersion`; timestamps omitted by default for determinism |

Disc numbers are unique; track numbers are unique within a disc; object paths
are unique across the whole package.

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

Three distinct concepts, never merged:

- **`identifiers`** — durable IDs (MusicBrainz release ID, barcode, per-track
  ISRC).
- **`identity`** — how the IDs were matched:
  - `source`: `musicbrainz` | `store` | `local`
  - `confidence`: `exact` | `confirmed` | `probable` | `none`
  Fuzzy matches are recorded as `probable` or `local`; they are **never**
  silently promoted to authoritative identity.
- **`source` / per-track `source`** — origin of the content (e.g.
  `{ "type": "digital-download", "store": "Deezer", "sourceId": "..." }`,
  `{ "type": "cd-rip" }`; per-track `{ "store": "Deezer", "trackId": "..." }`).
- **`provenance`** — how the package itself was built (`tool`, `toolVersion`).
- **`sourceAudio`** (per-track, optional) — the pre-encoding source file
  (`{ "codec": "flac", "md5": "..." }`), for tracing without implying identity.

## 5. Loudness (BS.1770 only)

Canonical `.mpack` loudness is measured with **ITU-R BS.1770-4** only.
Classic ReplayGain read from `.mpc` is import-time compatibility data and is
never canonical MusicPack loudness.

- Stored per track: `trackLUFS` (integrated) and `truePeakDbTP`.
- Stored per album (package): `albumLUFS` and `albumTruePeakDbTP`
  (integrated over the whole release).
- **Gain is derived, not stored**: `gain_db = target_lufs - measured_lufs`.
  The library provides `musicpack_loudness_compute_gain()`. No playback
  target is hard-coded by the format.
- Measured loudness is separate from any future playback-policy target.

## 6. Integrity

SHA-256 (lowercase hex) per audio/artwork/booklet/lyrics asset.
`musicpack verify` detects: missing files, checksum mismatches, malformed
manifests, duplicate track identity, invalid paths, impossible numbering,
invalid loudness values, unsupported manifest version, and (as warnings)
unreferenced files.

## 7. Validation / forward compatibility

- Unknown fields are **ignored on read** and **preserved on write**: a
  read-modify-write round-trip keeps extension data (patch-on-original).
- Unknown schema majors are rejected cleanly (`version` != 1).
- New fields must be added as optional fields; existing fields must not
  change meaning.

## 8. Security model

`.mpack` is untrusted input. The library enforces the path rules above,
bounds manifest size (16 MiB), bounds JSON nesting (100), validates checksum
format and loudness ranges, and treats `extras/` as opaque data (never
executed or interpreted). Fuzzing targets cover manifest parsing, path
normalization and package validation.
