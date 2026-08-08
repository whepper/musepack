/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see library.h)
*/
#include "library.h"
#include "mime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sqlite3.h>

struct mp_library {
    mp_db *db;
};

/* ---- lifecycle --------------------------------------------------------- */

mp_library *
mp_library_open(const char *path, int writable, char *err, size_t errcap)
{
    mp_library *lib = (mp_library *) calloc(1, sizeof *lib);
    if (lib == 0)
        return 0;
    if (mp_db_open(&lib->db, path, writable, err, errcap) != 0) {
        free(lib);
        return 0;
    }
    return lib;
}

void
mp_library_close(mp_library *lib)
{
    if (lib == 0)
        return;
    mp_db_close(lib->db);
    free(lib);
}

sqlite3 *
mp_library_sqlite(mp_library *lib)
{
    return lib != 0 ? mp_db_sqlite(lib->db) : 0;
}

int
mp_library_schema_version(mp_library *lib)
{
    return lib != 0 ? mp_db_schema_version(lib->db) : 0;
}

int
mp_library_begin(mp_library *lib)
{
    return sqlite3_exec(mp_db_sqlite(lib->db), "BEGIN;", 0, 0, 0);
}

int
mp_library_commit(mp_library *lib)
{
    return sqlite3_exec(mp_db_sqlite(lib->db), "COMMIT;", 0, 0, 0);
}

void
mp_library_rollback(mp_library *lib)
{
    sqlite3_exec(mp_db_sqlite(lib->db), "ROLLBACK;", 0, 0, 0);
}

/* ---- prepared statement helper ---------------------------------------- */

typedef struct {
    sqlite3_stmt *st;
} stmt;

static sqlite3_stmt *
stmt_prepare(mp_library *lib, const char *sql)
{
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(mp_db_sqlite(lib->db), sql, -1, &st, 0)
        != SQLITE_OK)
        return 0;
    return st;
}

/* ---- package rows ------------------------------------------------------ */

/* Copies a possibly-NULL text column into a buffer (empty when NULL). */
static void
col_cpy(char *dst, size_t cap, sqlite3_stmt *st, int idx)
{
    const unsigned char *t;
    if (cap == 0)
        return;
    dst[0] = '\0';
    t = sqlite3_column_text(st, idx);
    if (t != 0)
        snprintf(dst, cap, "%s", (const char *) t);
}

static void
fill_package_row(sqlite3_stmt *st, mp_package_row *row)
{
    memset(row, 0, sizeof *row);
    row->id = sqlite3_column_int64(st, 0);
    row->release_id = sqlite3_column_int64(st, 1);
    col_cpy(row->path, sizeof row->path, st, 2);
    col_cpy(row->fingerprint, sizeof row->fingerprint, st, 3);
    col_cpy(row->manifest_sha256, sizeof row->manifest_sha256, st, 4);
    col_cpy(row->status, sizeof row->status, st, 5);
    col_cpy(row->verify_status, sizeof row->verify_status, st, 6);
}

int
mp_library_package_by_path(mp_library *lib, const char *path,
                           mp_package_row *row)
{
    sqlite3_stmt *st = stmt_prepare(lib,
        "SELECT id, release_id, path, fingerprint, manifest_sha256,"
        "       status, verify_status FROM packages WHERE path = ?1");
    int rc = 0;
    if (st == 0)
        return 0;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        fill_package_row(st, row);
        rc = 1;
    }
    sqlite3_finalize(st);
    return rc;
}

int
mp_library_package_by_fingerprint(mp_library *lib, const char *fp,
                                  mp_package_row *row)
{
    sqlite3_stmt *st = stmt_prepare(lib,
        "SELECT id, release_id, path, fingerprint, manifest_sha256,"
        "       status, verify_status FROM packages WHERE fingerprint = ?1"
        " ORDER BY id LIMIT 1");
    int rc = 0;
    if (st == 0)
        return 0;
    sqlite3_bind_text(st, 1, fp, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        fill_package_row(st, row);
        rc = 1;
    }
    sqlite3_finalize(st);
    return rc;
}

long long
mp_library_package_insert(mp_library *lib, const char *path,
                          long long release_id, const char *fingerprint,
                          const char *manifest_sha256, const char *status,
                          const char *verify_status, const char *last_scan,
                          const char *last_error)
{
    sqlite3_stmt *st = stmt_prepare(lib,
        "INSERT INTO packages(path, release_id, fingerprint, manifest_sha256,"
        "  status, verify_status, last_scan, last_error)"
        " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
    if (st == 0)
        return -1;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
    if (release_id < 0)
        sqlite3_bind_null(st, 2);
    else
        sqlite3_bind_int64(st, 2, release_id);
    sqlite3_bind_text(st, 3, fingerprint, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, manifest_sha256, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, verify_status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, last_scan, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, last_error, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return -1;
    }
    sqlite3_finalize(st);
    return sqlite3_last_insert_rowid(mp_db_sqlite(lib->db));
}

int
mp_library_package_update(mp_library *lib, long long id, long long release_id,
                          const char *path, const char *fingerprint,
                          const char *manifest_sha256, const char *status,
                          const char *verify_status, const char *last_scan,
                          const char *last_error)
{
    sqlite3_stmt *st = stmt_prepare(lib,
        "UPDATE packages SET release_id=?2, path=?3, fingerprint=?4,"
        "  manifest_sha256=?5, status=?6, verify_status=?7, last_scan=?8,"
        "  last_error=?9, updated_at=datetime('now') WHERE id=?1");
    int rc;
    if (st == 0)
        return -1;
    sqlite3_bind_int64(st, 1, id);
    if (release_id < 0)
        sqlite3_bind_null(st, 2);
    else
        sqlite3_bind_int64(st, 2, release_id);
    sqlite3_bind_text(st, 3, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, fingerprint, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, manifest_sha256, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, verify_status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, last_scan, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, last_error, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    return rc;
}

int
mp_library_package_sweep(mp_library *lib, const char *last_scan)
{
    sqlite3_stmt *st = stmt_prepare(lib,
        "UPDATE packages SET status='unavailable', last_error='package "
        "directory not found', updated_at=datetime('now')"
        " WHERE last_scan != ?1 AND status != 'unavailable'");
    int changed = 0;
    if (st == 0)
        return 0;
    sqlite3_bind_text(st, 1, last_scan, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_DONE)
        changed = sqlite3_changes(mp_db_sqlite(lib->db));
    sqlite3_finalize(st);
    return changed;
}

/* ---- hierarchy writes --------------------------------------------------- */

long long
mp_library_upsert_artist(mp_library *lib, const char *name)
{
    sqlite3_stmt *st = stmt_prepare(lib,
        "INSERT INTO artists(name) VALUES (?1) ON CONFLICT(name) DO NOTHING");
    sqlite3 *db = mp_db_sqlite(lib->db);
    long long id = 0;
    if (st == 0)
        return -1;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    /* ON CONFLICT DO NOTHING returns SQLITE_DONE even on a no-op, so only
       trust last_insert_rowid() when a row was actually inserted. */
    if (sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0)
        id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(st);
    if (id == 0) {
        st = stmt_prepare(lib, "SELECT id FROM artists WHERE name = ?1");
        if (st == 0)
            return -1;
        sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW)
            id = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    return id;
}

static int
replace_group_artists(mp_library *lib, long long group_id,
                      const musicpack_manifest *m)
{
    sqlite3_stmt *st;
    size_t i;
    if (sqlite3_exec(mp_db_sqlite(lib->db),
                     "DELETE FROM group_artists WHERE group_id = ?1", 0, 0, 0)
        != SQLITE_OK) {
        /* not reached; prepared below */
    }
    st = stmt_prepare(lib, "DELETE FROM group_artists WHERE group_id = ?1");
    if (st == 0)
        return -1;
    sqlite3_bind_int64(st, 1, group_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    for (i = 0; i < m->album_artist_count; i++) {
        long long artist_id = mp_library_upsert_artist(lib,
                                                        m->album_artists[i].name);

        st = stmt_prepare(lib,
            "INSERT INTO group_artists(group_id, artist_id, position, role)"
            " VALUES (?1, ?2, ?3, ?4)");
        if (st == 0)
            return -1;
        sqlite3_bind_int64(st, 1, group_id);
        sqlite3_bind_int64(st, 2, artist_id);
        sqlite3_bind_int64(st, 3, (long long) i);
        sqlite3_bind_text(st, 4, m->album_artists[i].role, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    return 0;
}

long long
mp_library_upsert_group(mp_library *lib, const musicpack_manifest *m,
                        const char *group_key)
{
    sqlite3_stmt *st = stmt_prepare(lib,
        "SELECT id FROM release_groups WHERE group_key = ?1");
    long long id = -1;
    if (st == 0)
        return -1;
    sqlite3_bind_text(st, 1, group_key, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        id = sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);
    if (id < 0) {
        st = stmt_prepare(lib,
            "INSERT INTO release_groups(title, release_type,"
            " original_release_date, mbid, group_key)"
            " VALUES (?1, ?2, ?3, ?4, ?5)");
        if (st == 0)
            return -1;
        sqlite3_bind_text(st, 1, m->album_title, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, m->release_type, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, m->original_release_date, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, m->musicbrainz_release_group_id, -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, group_key, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) != SQLITE_DONE) {
            sqlite3_finalize(st);
            return -1;
        }
        sqlite3_finalize(st);
        id = sqlite3_last_insert_rowid(mp_db_sqlite(lib->db));
    } else {
        st = stmt_prepare(lib,
            "UPDATE release_groups SET title=?1, release_type=?2,"
            " original_release_date=?3, mbid=?4, updated_at=datetime('now')"
            " WHERE id=?5");
        if (st == 0)
            return -1;
        sqlite3_bind_text(st, 1, m->album_title, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, m->release_type, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, m->original_release_date, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, m->musicbrainz_release_group_id, -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 5, id);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    replace_group_artists(lib, id, m);
    return id;
}

long long
mp_library_upsert_release(mp_library *lib, const musicpack_manifest *m,
                          long long group_id, const char *release_key)
{
    sqlite3_stmt *st = stmt_prepare(lib,
        "SELECT id FROM releases WHERE group_id = ?1 AND release_key = ?2");
    long long id = -1;
    if (st == 0)
        return -1;
    sqlite3_bind_int64(st, 1, group_id);
    sqlite3_bind_text(st, 2, release_key, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW)
        id = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    if (id < 0) {
        st = stmt_prepare(lib,
            "INSERT INTO releases(group_id, edition, release_date, country,"
            "  label, catalogue_number, notes, barcode, mbid, release_key,"
            "  source_type, source_store, source_id, identity_source,"
            "  identity_confidence, provenance_tool, provenance_tool_version)"
            " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17)");
        if (st == 0)
            return -1;
        sqlite3_bind_int64(st, 1, group_id);
        sqlite3_bind_text(st, 2, m->release.edition, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, m->release.release_date, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, m->release.country, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, m->release.label, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 6, m->release.catalogue_number, -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 7, m->release.notes, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 8, m->barcode, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 9, m->musicbrainz_release_id, -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 10, release_key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 11, m->source_type, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 12, m->source_store, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 13, m->source_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 14, m->identity_source, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 15, m->identity_confidence, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 16, m->provenance_tool, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 17, m->provenance_tool_version, -1,
                          SQLITE_TRANSIENT);
        if (sqlite3_step(st) != SQLITE_DONE) {
            sqlite3_finalize(st);
            return -1;
        }
        sqlite3_finalize(st);
        id = sqlite3_last_insert_rowid(mp_db_sqlite(lib->db));
    } else {
        st = stmt_prepare(lib,
            "UPDATE releases SET edition=?2, release_date=?3, country=?4,"
            " label=?5, catalogue_number=?6, notes=?7, barcode=?8, mbid=?9,"
            " source_type=?10, source_store=?11, source_id=?12,"
            " identity_source=?13, identity_confidence=?14,"
            " provenance_tool=?15, provenance_tool_version=?16,"
            " updated_at=datetime('now') WHERE id=?1");
        if (st == 0)
            return -1;
        sqlite3_bind_int64(st, 1, id);
        sqlite3_bind_text(st, 2, m->release.edition, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, m->release.release_date, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, m->release.country, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, m->release.label, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 6, m->release.catalogue_number, -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 7, m->release.notes, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 8, m->barcode, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 9, m->musicbrainz_release_id, -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 10, m->source_type, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 11, m->source_store, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 12, m->source_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 13, m->identity_source, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 14, m->identity_confidence, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 15, m->provenance_tool, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 16, m->provenance_tool_version, -1,
                          SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    return id;
}

static long long
file_size_of(const char *path)
{
    struct stat st;
    if (path == 0 || stat(path, &st) != 0)
        return 0;
    return (long long) st.st_size;
}

static int
insert_track_artists(mp_library *lib, long long track_id,
                     const musicpack_track *t)
{
    size_t i;
    for (i = 0; i < t->artist_count; i++) {
        long long aid = mp_library_upsert_artist(lib, t->artists[i].name);
        sqlite3_stmt *st = stmt_prepare(lib,
            "INSERT INTO track_artists(track_id, artist_id, position, role)"
            " VALUES (?1,?2,?3,?4)");
        if (st == 0)
            return -1;
        sqlite3_bind_int64(st, 1, track_id);
        sqlite3_bind_int64(st, 2, aid);
        sqlite3_bind_int64(st, 3, (long long) i);
        sqlite3_bind_text(st, 4, t->artists[i].role, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    return 0;
}

static int
insert_asset_row(mp_library *lib, long long release_id, const char *root,
                 const char *kind, const char *role,
                 const char *rel, const char *sha)
{
    char abs[MUSICPACK_PATH_MAX + 2];
    sqlite3_stmt *st;
    long long size;

    if (musicpack_path_resolve(root, rel, abs, sizeof abs) != MUSICPACK_OK)
        return 0;
    size = file_size_of(abs);
    st = stmt_prepare(lib,
        "INSERT INTO assets(release_id, kind, role, relative_path, sha256,"
        "  file_size, mime_type) VALUES (?1,?2,?3,?4,?5,?6,?7)");
    if (st == 0)
        return -1;
    sqlite3_bind_int64(st, 1, release_id);
    sqlite3_bind_text(st, 2, kind, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, role, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, rel, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, sha, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 6, size);
    sqlite3_bind_text(st, 7, mp_mime_for_path(rel), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return 0;
}

static int
insert_asset_list(mp_library *lib, long long release_id, const char *root,
                  const char *kind, const musicpack_asset *a, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (insert_asset_row(lib, release_id, root, kind, 0, a[i].path,
                             a[i].sha256) != 0)
            return -1;
    }
    return 0;
}

static int
insert_artwork(mp_library *lib, long long release_id, const char *root,
               const musicpack_artwork *a, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (insert_asset_row(lib, release_id, root, "artwork",
                             a[i].role, a[i].asset.path, a[i].asset.sha256) != 0)
            return -1;
    }
    return 0;
}

int
mp_library_replace_release_content(mp_library *lib, long long release_id,
                                   const musicpack_manifest *m,
                                   const char *root,
                                   const mp_track_ingest *codecs,
                                   size_t codec_count)
{
    sqlite3_stmt *st;
    size_t d, t;
    size_t ci = 0;

    st = stmt_prepare(lib, "DELETE FROM media WHERE release_id = ?1");
    if (st == 0)
        return -1;
    sqlite3_bind_int64(st, 1, release_id);
    sqlite3_step(st);
    sqlite3_finalize(st);

    for (d = 0; d < m->disc_count; d++) {
        const musicpack_disc *disc = &m->discs[d];
        long long media_id;
        st = stmt_prepare(lib,
            "INSERT INTO media(release_id, disc_number, format, title, position)"
            " VALUES (?1,?2,?3,?4,?5)");
        if (st == 0)
            return -1;
        sqlite3_bind_int64(st, 1, release_id);
        sqlite3_bind_int64(st, 2, disc->disc);
        sqlite3_bind_text(st, 3, disc->format, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, disc->title, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 5, (long long) d);
        if (sqlite3_step(st) != SQLITE_DONE) {
            sqlite3_finalize(st);
            return -1;
        }
        sqlite3_finalize(st);
        media_id = sqlite3_last_insert_rowid(mp_db_sqlite(lib->db));

        for (t = 0; t < disc->track_count; t++) {
            const musicpack_track *tr = &disc->tracks[t];
            long long track_id;
            const mp_track_ingest *ti = ci < codec_count ? &codecs[ci] : 0;
            char codec[24];
            const char *sha = tr->audio.sha256;
            long long size = 0;

            ci++;
            st = stmt_prepare(lib,
                "INSERT INTO tracks(media_id, track_number, title, isrc,"
                "  mbid_track, mbid_recording, source_store, source_track_id,"
                "  source_audio_codec, source_audio_md5, has_duration,"
                "  duration, has_loudness, loudness_lufs, loudness_true_peak_db)"
                " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15)");
            if (st == 0)
                return -1;
            sqlite3_bind_int64(st, 1, media_id);
            sqlite3_bind_int64(st, 2, tr->number);
            sqlite3_bind_text(st, 3, tr->title, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, tr->isrc, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 5, tr->musicbrainz_track_id, -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 6, tr->musicbrainz_recording_id, -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 7, tr->source_store, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 8, tr->source_track_id, -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 9, tr->source_audio_codec, -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 10, tr->source_audio_md5, -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 11, tr->has_duration);
            sqlite3_bind_double(st, 12, tr->duration);
            sqlite3_bind_int(st, 13, tr->loudness.present);
            sqlite3_bind_double(st, 14, tr->loudness.lufs);
            sqlite3_bind_double(st, 15, tr->loudness.true_peak_db);
            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                return -1;
            }
            sqlite3_finalize(st);
            track_id = sqlite3_last_insert_rowid(mp_db_sqlite(lib->db));

            insert_track_artists(lib, track_id, tr);

            if (ti != 0) {
                size = file_size_of(ti->abs_path);
                snprintf(codec, sizeof codec, "%s",
                         ti->codec.codec[0] != '\0'
                             ? ti->codec.codec
                             : mp_codec_for_path(tr->audio.path));
            } else {
                snprintf(codec, sizeof codec, "%s",
                         mp_codec_for_path(tr->audio.path));
            }
            st = stmt_prepare(lib,
                "INSERT INTO audio_objects(track_id, relative_path, sha256,"
                "  file_size, mime_type, codec, stream_version, sample_rate,"
                "  channels) VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9)");
            if (st == 0)
                return -1;
            sqlite3_bind_int64(st, 1, track_id);
            sqlite3_bind_text(st, 2, tr->audio.path, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, sha, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 4, size);
            sqlite3_bind_text(st, 5, mp_mime_for_path(tr->audio.path), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 6, codec, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 7, ti != 0 ? ti->codec.stream_version : 0);
            sqlite3_bind_int64(st, 8, ti != 0 ? ti->codec.sample_rate : 0);
            sqlite3_bind_int64(st, 9, ti != 0 ? ti->codec.channels : 0);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }

    insert_artwork(lib, release_id, root, m->artwork, m->artwork_count);
    insert_asset_list(lib, release_id, root, "booklet", m->booklet,
                      m->booklet_count);
    insert_asset_list(lib, release_id, root, "lyrics", m->lyrics,
                      m->lyrics_count);
    insert_asset_list(lib, release_id, root, "extras", m->extras,
                      m->extras_count);
    return 0;
}

/* ---- object resolution for streaming ----------------------------------- */

static void
fill_object_ref(sqlite3_stmt *st, mp_object_ref *ref)
{
    memset(ref, 0, sizeof *ref);
    ref->id = sqlite3_column_int64(st, 0);
    ref->release_id = sqlite3_column_int64(st, 1);
    col_cpy(ref->package_path, sizeof ref->package_path, st, 2);
    col_cpy(ref->relative_path, sizeof ref->relative_path, st, 3);
    col_cpy(ref->mime, sizeof ref->mime, st, 4);
    col_cpy(ref->codec, sizeof ref->codec, st, 5);
    ref->file_size = sqlite3_column_int64(st, 6);
    col_cpy(ref->status, sizeof ref->status, st, 7);
    ref->stream_version = sqlite3_column_int(st, 8);
    ref->sample_rate = sqlite3_column_int64(st, 9);
    ref->channels = sqlite3_column_int64(st, 10);
    col_cpy(ref->sha256, sizeof ref->sha256, st, 11);
}

int
mp_library_track_audio(mp_library *lib, long long track_id, mp_object_ref *ref)
{
    sqlite3_stmt *st = stmt_prepare(lib,
        "SELECT a.id, r.id, p.path, a.relative_path, a.mime_type, a.codec,"
        "       a.file_size, p.status, a.stream_version, a.sample_rate,"
        "       a.channels, a.sha256"
        "  FROM tracks t"
        "  JOIN media me ON me.id = t.media_id"
        "  JOIN releases r ON r.id = me.release_id"
        "  JOIN packages p ON p.release_id = r.id"
        "  JOIN audio_objects a ON a.track_id = t.id"
        " WHERE t.id = ?1"
        " ORDER BY CASE WHEN p.status='unavailable' THEN 1 ELSE 0 END, p.id"
        " LIMIT 1");
    int rc = 0;
    if (st == 0)
        return 0;
    sqlite3_bind_int64(st, 1, track_id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        fill_object_ref(st, ref);
        rc = 1;
    }
    sqlite3_finalize(st);
    return rc;
}

int
mp_library_asset(mp_library *lib, long long asset_id, mp_object_ref *ref)
{
    sqlite3_stmt *st = stmt_prepare(lib,
        "SELECT a.id, r.id, p.path, a.relative_path, a.mime_type, '',"
        "       a.file_size, p.status, 0, 0, 0, a.sha256"
        "  FROM assets a"
        "  JOIN releases r ON r.id = a.release_id"
        "  JOIN packages p ON p.release_id = r.id"
        " WHERE a.id = ?1 AND a.kind IN ('artwork','booklet','lyrics')"
        " ORDER BY CASE WHEN p.status='unavailable' THEN 1 ELSE 0 END, p.id"
        " LIMIT 1");
    int rc = 0;
    if (st == 0)
        return 0;
    sqlite3_bind_int64(st, 1, asset_id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        fill_object_ref(st, ref);
        rc = 1;
    }
    sqlite3_finalize(st);
    return rc;
}
