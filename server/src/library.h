/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

  * Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the following
  disclaimer in the documentation and/or other materials provided
  with the distribution.

  * Neither the name of the The MusicPack Development Team nor the
  names of its contributors may be used to endorse or promote
  products derived from this software without specific prior
  written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/// \file library.h
/// The durable library database: scanner-facing writes + queries.
///
/// This is the only component that writes SQL. The HTTP layer and tests read
/// through it (or through raw sqlite3 for API JSON shaping). Write paths run
/// inside an explicit transaction begun by the caller (one per package), so a
/// malformed package rolls back without corrupting the index.
#ifndef MPSERVER_LIBRARY_H_
#define MPSERVER_LIBRARY_H_

#include <stddef.h>

#include <musicpack/musicpack.h>

#include "codec.h"
#include "db.h"
#include "identity.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mp_library mp_library;

/* ---- lifecycle --------------------------------------------------------- */

mp_library *mp_library_open(const char *path, int writable,
                            char *err, size_t errcap);
void mp_library_close(mp_library *lib);
sqlite3 *mp_library_sqlite(mp_library *lib);
/// Current schema version (for health reporting).
int mp_library_schema_version(mp_library *lib);

int mp_library_begin(mp_library *lib);
int mp_library_commit(mp_library *lib);
void mp_library_rollback(mp_library *lib);

/* ---- package rows ------------------------------------------------------ */

typedef struct mp_package_row {
    long long id;
    long long release_id;
    char path[MUSICPACK_PATH_MAX + 2];
    char fingerprint[MP_ID_KEY_MAX];
    char manifest_sha256[MUSICPACK_SHA256_HEX_SIZE];
    char status[32];
    char verify_status[32];
} mp_package_row;

/// Finds a package by absolute path. Returns 1 and fills \p row, else 0.
int mp_library_package_by_path(mp_library *lib, const char *path,
                               mp_package_row *row);
/// Finds a package by content fingerprint. Returns 1 and fills \p row, else 0.
int mp_library_package_by_fingerprint(mp_library *lib, const char *fp,
                                      mp_package_row *row);

/// Inserts a package row; returns its id (or -1 on failure).
long long mp_library_package_insert(mp_library *lib, const char *path,
                                    long long release_id,
                                    const char *fingerprint,
                                    const char *manifest_sha256,
                                    const char *status,
                                    const char *verify_status,
                                    const char *last_scan,
                                    const char *last_error);
/// Updates an existing package row.
int mp_library_package_update(mp_library *lib, long long id,
                              long long release_id, const char *path,
                              const char *fingerprint,
                              const char *manifest_sha256,
                              const char *status, const char *verify_status,
                              const char *last_scan, const char *last_error);

/// Marks every package not seen in scan \p last_scan as unavailable.
/// Returns the number of packages newly marked (for removal logging).
int mp_library_package_sweep(mp_library *lib, const char *last_scan);

/* ---- hierarchy writes (inside a transaction) -------------------------- */

/// Resolves an artist name to its id, inserting when absent.
long long mp_library_upsert_artist(mp_library *lib, const char *name);

/// Upserts the release group (album) for \p m; returns its id.
long long mp_library_upsert_group(mp_library *lib, const musicpack_manifest *m,
                                  const char *group_key);

/// Upserts the specific release/edition for \p m under \p group_id;
/// returns its id.
long long mp_library_upsert_release(mp_library *lib,
                                    const musicpack_manifest *m,
                                    long long group_id,
                                    const char *release_key);

typedef struct mp_track_ingest {
    char abs_path[MUSICPACK_PATH_MAX + 2]; ///< resolved audio object path
    mp_codec_info codec;
} mp_track_ingest;

/// Replaces all media/tracks/audio/assets under \p release_id from \p m.
/// \p codecs must contain one entry per track in manifest order
/// (disc-major, then track order).
int mp_library_replace_release_content(mp_library *lib, long long release_id,
                                       const musicpack_manifest *m,
                                       const char *root,
                                       const mp_track_ingest *codecs,
                                       size_t codec_count);

/* ---- object resolution for streaming ---------------------------------- */

typedef struct mp_object_ref {
    long long id;          ///< audio_objects.id or assets.id
    long long release_id;
    char package_path[MUSICPACK_PATH_MAX + 2]; ///< package root (absolute)
    char relative_path[MUSICPACK_PATH_MAX + 2]; ///< manifest-relative
    char mime[48];
    char codec[24];
    long long file_size;
    char status[32];       ///< package status: valid/warning/.../unavailable
    int stream_version;
    long long sample_rate;
    long long channels;
} mp_object_ref;

/// Looks up a track's audio object by track id. Returns 1 on success.
int mp_library_track_audio(mp_library *lib, long long track_id,
                           mp_object_ref *ref);

/// Looks up an asset by id and kind whitelist (artwork/booklet/lyrics).
/// Returns 1 on success, 0 if not found or kind not servable.
int mp_library_asset(mp_library *lib, long long asset_id, mp_object_ref *ref);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_LIBRARY_H_ */
