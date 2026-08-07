/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see schema.h)
*/
#include "schema.h"

/*
  Migration 1: the Phase 4 library schema.

  Collector hierarchy (frozen .mpack v1 semantics preserved, never flattened):

    release_groups (album)  -> releases (specific edition) -> media (disc)
                                                            -> tracks
    group_artists / track_artists   -> artists (normalized)
    tracks 1:1 audio_objects        (codec/size/sha, streaming source)
    releases 1:N assets             (artwork/booklet/lyrics)
    packages -> releases            (where a release is stored on disk)

  Identity strategy (see identity.h): packages carry a content fingerprint
  (sha256 of the canonical manifest) used for change + move detection; groups
  and releases carry stable keys so moving a package does not change its
  logical identity. status values follow musicpack verify semantics plus a
  server-side 'invalid' for manifest parse failures.
*/
static const char *const mp_migrations[] = {
/* 0 -> 1 */
"CREATE TABLE artists ("
"  id INTEGER PRIMARY KEY,"
"  name TEXT NOT NULL UNIQUE COLLATE NOCASE,"
"  sort_name TEXT);"

"CREATE TABLE release_groups ("
"  id INTEGER PRIMARY KEY,"
"  title TEXT NOT NULL,"
"  release_type TEXT,"
"  original_release_date TEXT,"
"  mbid TEXT UNIQUE,"
"  group_key TEXT NOT NULL UNIQUE,"
"  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
"  updated_at TEXT NOT NULL DEFAULT (datetime('now')));"

"CREATE TABLE group_artists ("
"  group_id INTEGER NOT NULL REFERENCES release_groups(id) ON DELETE CASCADE,"
"  artist_id INTEGER NOT NULL REFERENCES artists(id),"
"  position INTEGER NOT NULL,"
"  role TEXT,"
"  PRIMARY KEY (group_id, position));"

"CREATE TABLE releases ("
"  id INTEGER PRIMARY KEY,"
"  group_id INTEGER NOT NULL REFERENCES release_groups(id) ON DELETE CASCADE,"
"  edition TEXT,"
"  release_date TEXT,"
"  country TEXT,"
"  label TEXT,"
"  catalogue_number TEXT,"
"  notes TEXT,"
"  barcode TEXT,"
"  mbid TEXT,"
"  release_key TEXT NOT NULL,"
"  source_type TEXT,"
"  source_store TEXT,"
"  source_id TEXT,"
"  identity_source TEXT,"
"  identity_confidence TEXT,"
"  provenance_tool TEXT,"
"  provenance_tool_version TEXT,"
"  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
"  updated_at TEXT NOT NULL DEFAULT (datetime('now')));"
"CREATE UNIQUE INDEX releases_key_idx ON releases(group_id, release_key);"

"CREATE TABLE media ("
"  id INTEGER PRIMARY KEY,"
"  release_id INTEGER NOT NULL REFERENCES releases(id) ON DELETE CASCADE,"
"  disc_number INTEGER NOT NULL,"
"  format TEXT,"
"  title TEXT,"
"  position INTEGER NOT NULL);"
"CREATE INDEX media_release_idx ON media(release_id);"

"CREATE TABLE tracks ("
"  id INTEGER PRIMARY KEY,"
"  media_id INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,"
"  track_number INTEGER NOT NULL,"
"  title TEXT NOT NULL,"
"  isrc TEXT,"
"  mbid_track TEXT,"
"  mbid_recording TEXT,"
"  source_store TEXT,"
"  source_track_id TEXT,"
"  source_audio_codec TEXT,"
"  source_audio_md5 TEXT,"
"  has_duration INTEGER NOT NULL DEFAULT 0,"
"  duration REAL,"
"  has_loudness INTEGER NOT NULL DEFAULT 0,"
"  loudness_lufs REAL,"
"  loudness_true_peak_db REAL);"
"CREATE INDEX tracks_media_idx ON tracks(media_id);"

"CREATE TABLE track_artists ("
"  track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,"
"  artist_id INTEGER NOT NULL REFERENCES artists(id),"
"  position INTEGER NOT NULL,"
"  role TEXT,"
"  PRIMARY KEY (track_id, position));"

"CREATE TABLE audio_objects ("
"  id INTEGER PRIMARY KEY,"
"  track_id INTEGER NOT NULL UNIQUE REFERENCES tracks(id) ON DELETE CASCADE,"
"  relative_path TEXT NOT NULL,"
"  sha256 TEXT,"
"  file_size INTEGER NOT NULL DEFAULT 0,"
"  mime_type TEXT NOT NULL,"
"  codec TEXT NOT NULL,"
"  stream_version INTEGER,"
"  sample_rate INTEGER,"
"  channels INTEGER);"

"CREATE TABLE assets ("
"  id INTEGER PRIMARY KEY,"
"  release_id INTEGER NOT NULL REFERENCES releases(id) ON DELETE CASCADE,"
"  kind TEXT NOT NULL,"
"  role TEXT,"
"  relative_path TEXT NOT NULL,"
"  sha256 TEXT,"
"  file_size INTEGER NOT NULL DEFAULT 0,"
"  mime_type TEXT NOT NULL);"
"CREATE INDEX assets_release_idx ON assets(release_id);"

"CREATE TABLE packages ("
"  id INTEGER PRIMARY KEY,"
"  path TEXT NOT NULL UNIQUE,"
"  release_id INTEGER REFERENCES releases(id) ON DELETE CASCADE,"
"  fingerprint TEXT NOT NULL,"
"  manifest_sha256 TEXT NOT NULL,"
"  status TEXT NOT NULL DEFAULT 'valid',"
"  verify_status TEXT NOT NULL DEFAULT 'unverified',"
"  last_scan TEXT NOT NULL DEFAULT '',"
"  last_error TEXT,"
"  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
"  updated_at TEXT NOT NULL DEFAULT (datetime('now')));"
"CREATE INDEX packages_fingerprint_idx ON packages(fingerprint);"
"CREATE INDEX packages_release_idx ON packages(release_id);"
};

const char *const *
mp_schema_migrations(void)
{
    return mp_migrations;
}

int
mp_schema_migration_count(void)
{
    return (int) (sizeof mp_migrations / sizeof *mp_migrations);
}
