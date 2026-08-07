/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see api.h)
*/
#include "api.h"
#include "json.h"
#include "log.h"
#include "mime.h"
#include "range.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sqlite3.h>
#include <microhttpd.h>

#include <musicpack/musicpack.h>

#ifndef O_NOFOLLOW
# define O_NOFOLLOW 0
#endif

#define API_VERSION "v1"
#define VISIBLE "p.status NOT IN ('invalid','unavailable')"

/* ---------- small parsing helpers -------------------------------------- */

static int
parse_id(const char *s, long long *out)
{
    long long v = 0;
    size_t n = 0;

    if (s == 0 || *s == '\0')
        return 0;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9')
            return 0;
        v = v * 10 + (*s - '0');
        if (++n > 18 || v > 0x7fffffffffffffffLL)
            return 0;
    }
    *out = v;
    return 1;
}

static int
parse_paged(const char *value, int deflt, int min, int max, int *out)
{
    int v = deflt;
    if (value != 0) {
        long long n;
        if (!parse_id(value, &n))
            return 0;
        v = (int) n;
    }
    if (v < min)
        v = min;
    if (v > max)
        v = max;
    *out = v;
    return 1;
}

static const char *
col_text(sqlite3_stmt *st, int i)
{
    const unsigned char *t = sqlite3_column_text(st, i);
    return t != 0 ? (const char *) t : 0;
}

/* ---------- response helpers ------------------------------------------- */

static struct MHD_Response *
json_response(const char *json, unsigned int status)
{
    struct MHD_Response *r =
        MHD_create_response_from_buffer(strlen(json), (void *) json,
                                        MHD_RESPMEM_MUST_COPY);
    (void) status;
    if (r != 0) {
        MHD_add_response_header(r, MHD_HTTP_HEADER_CONTENT_TYPE,
                                "application/json; charset=utf-8");
        MHD_add_response_header(r, "Cache-Control", "no-store");
    }
    return r;
}

static struct MHD_Response *
error_response(unsigned int status, const char *code, const char *message)
{
    char *body = mp_json_error(code, message);
    struct MHD_Response *r = json_response(body, status);
    free(body);
    return r;
}

/* ---------- JSON builders ---------------------------------------------- */

static mp_json *
artists_of_group(mp_library *lib, long long group_id)
{
    sqlite3 *db = mp_library_sqlite(lib);
    sqlite3_stmt *st;
    mp_json *arr = mp_json_arr();

    if (sqlite3_prepare_v2(db,
            "SELECT a.id, a.name, ga.role FROM group_artists ga"
            " JOIN artists a ON a.id = ga.artist_id"
            " WHERE ga.group_id = ?1 ORDER BY ga.position", -1, &st, 0)
        != SQLITE_OK)
        return arr;
    sqlite3_bind_int64(st, 1, group_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        mp_json *o = mp_json_obj();
        mp_json_int(o, "id", sqlite3_column_int64(st, 0));
        mp_json_str(o, "name", col_text(st, 1));
        mp_json_str_opt(o, "role", col_text(st, 2));
        mp_json_add(arr, 0, o);
    }
    sqlite3_finalize(st);
    return arr;
}

static mp_json *
artists_of_track(mp_library *lib, long long track_id)
{
    sqlite3 *db = mp_library_sqlite(lib);
    sqlite3_stmt *st;
    mp_json *arr = mp_json_arr();

    if (sqlite3_prepare_v2(db,
            "SELECT a.id, a.name, ta.role FROM track_artists ta"
            " JOIN artists a ON a.id = ta.artist_id"
            " WHERE ta.track_id = ?1 ORDER BY ta.position", -1, &st, 0)
        != SQLITE_OK)
        return arr;
    sqlite3_bind_int64(st, 1, track_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        mp_json *o = mp_json_obj();
        mp_json_int(o, "id", sqlite3_column_int64(st, 0));
        mp_json_str(o, "name", col_text(st, 1));
        mp_json_str_opt(o, "role", col_text(st, 2));
        mp_json_add(arr, 0, o);
    }
    sqlite3_finalize(st);
    return arr;
}

static mp_json *
media_formats_of_release(mp_library *lib, long long release_id)
{
    sqlite3 *db = mp_library_sqlite(lib);
    sqlite3_stmt *st;
    mp_json *arr = mp_json_arr();

    if (sqlite3_prepare_v2(db,
            "SELECT format FROM media WHERE release_id = ?1 AND format IS NOT NULL"
            " GROUP BY format ORDER BY MIN(position)", -1, &st, 0)
        != SQLITE_OK)
        return arr;
    sqlite3_bind_int64(st, 1, release_id);
    while (sqlite3_step(st) == SQLITE_ROW)
        mp_json_add(arr, 0, mp_json_strnode(col_text(st, 0)));
    sqlite3_finalize(st);
    return arr;
}

static mp_json *
group_object(mp_library *lib, sqlite3_stmt *g)
{
    mp_json *o = mp_json_obj();
    mp_json_int(o, "id", sqlite3_column_int64(g, 0));
    mp_json_str(o, "title", col_text(g, 1));
    mp_json_str_opt(o, "releaseType", col_text(g, 2));
    mp_json_str_opt(o, "originalReleaseDate", col_text(g, 3));
    mp_json_str_opt(o, "mbid", col_text(g, 4));
    mp_json_add(o, "artists", artists_of_group(lib, sqlite3_column_int64(g, 0)));
    return o;
}

/* ---------- streaming --------------------------------------------------- */

static int
serveable(const mp_object_ref *ref)
{
    return strcmp(ref->status, "unavailable") != 0 &&
           strcmp(ref->status, "invalid") != 0;
}

static struct MHD_Response *
serve_object(mp_library *lib, const mp_object_ref *ref,
             struct MHD_Connection *c, unsigned int *status_out)
{
    char abs[MUSICPACK_PATH_MAX + 2];
    int fd;
    struct stat st;
    const char *range;
    mp_range r;
    struct MHD_Response *resp;

    (void) lib;
    if (!serveable(ref)) {
        *status_out = 503;
        return error_response(503, "unavailable",
                              "package unavailable; rescan the library");
    }
    if (musicpack_path_resolve(ref->package_path, ref->relative_path, abs,
                               sizeof abs) != MUSICPACK_OK) {
        *status_out = 503;
        return error_response(503, "unavailable", "audio object not found");
    }
    fd = open(abs, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        MP_LOGW("stream: cannot open %s", abs);
        *status_out = 503;
        return error_response(503, "unavailable", "source file missing");
    }
    if (fstat(fd, &st) != 0) {
        close(fd);
        *status_out = 500;
        return error_response(500, "internal", "cannot stat source file");
    }
    if (st.st_size < 0) {
        close(fd);
        *status_out = 500;
        return error_response(500, "internal", "invalid source file size");
    }

    range = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Range");
    if (range == 0) {
        resp = MHD_create_response_from_fd64((uint64_t) st.st_size, fd);
        if (resp == 0) {
            close(fd);
            *status_out = 500;
            return error_response(500, "internal", "cannot create response");
        }
        MHD_add_response_header(resp, "Content-Type", ref->mime);
        MHD_add_response_header(resp, "Accept-Ranges", "bytes");
        *status_out = 200;
        return resp;
    }

    switch (mp_range_parse(range, (long long) st.st_size, &r)) {
    case MP_RANGE_OK: {
        char cr[128];
        snprintf(cr, sizeof cr, "bytes %lld-%lld/%lld",
                 r.start, r.start + r.length - 1, (long long) st.st_size);
        resp = MHD_create_response_from_fd_at_offset64(
            (uint64_t) r.length, fd, (uint64_t) r.start);
        if (resp == 0) {
            close(fd);
            *status_out = 500;
            return error_response(500, "internal", "cannot create response");
        }
        MHD_add_response_header(resp, "Content-Type", ref->mime);
        MHD_add_response_header(resp, "Accept-Ranges", "bytes");
        MHD_add_response_header(resp, "Content-Range", cr);
        *status_out = 206;
        return resp;
    }
    case MP_RANGE_UNSATISFIABLE:
    case MP_RANGE_INVALID:
    default: {
        char cr[64];
        snprintf(cr, sizeof cr, "bytes */%lld", (long long) st.st_size);
        close(fd);
        resp = MHD_create_response_from_buffer(0, 0, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Range", cr);
        MHD_add_response_header(resp, "Accept-Ranges", "bytes");
        *status_out = 416;
        return resp;
    }
    }
}

/* ---------- route handlers ---------------------------------------------- */

static struct MHD_Response *
handle_health(mp_library *lib)
{
    mp_json *o = mp_json_obj();
    char *s;
    struct MHD_Response *r;
    mp_json_str(o, "status", "ok");
    mp_json_str(o, "version", MUSICPACK_VERSION);
    mp_json_str(o, "apiVersion", API_VERSION);
    mp_json_int(o, "schemaVersion", mp_library_schema_version(lib));
    s = mp_json_render(o);
    r = json_response(s, 200);
    free(s);
    mp_json_free(o);
    return r;
}

static struct MHD_Response *
handle_artists(mp_library *lib, struct MHD_Connection *c, unsigned int *st)
{
    sqlite3 *db = mp_library_sqlite(lib);
    sqlite3_stmt *qs, *cs;
    char *limit_s = (char *) MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, "limit");
    char *offset_s = (char *) MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, "offset");
    int limit, offset, total = 0;
    mp_json *o = mp_json_obj(), *arr = mp_json_arr();
    char *s;
    struct MHD_Response *r;

    if (!parse_paged(limit_s, 50, 1, 200, &limit) ||
        !parse_paged(offset_s, 0, 0, 100000, &offset)) {
        mp_json_free(o);
        *st = 400;
        return error_response(400, "invalid_request",
                              "limit/offset must be non-negative integers");
    }
    if (sqlite3_prepare_v2(db,
            "SELECT COUNT(*) FROM artists a WHERE EXISTS ("
            "  SELECT 1 FROM group_artists ga"
            "  JOIN releases r ON r.group_id = ga.group_id"
            "  JOIN packages p ON p.release_id = r.id"
            "  WHERE ga.artist_id = a.id AND " VISIBLE ")", -1, &cs, 0)
        == SQLITE_OK && sqlite3_step(cs) == SQLITE_ROW)
        total = sqlite3_column_int(cs, 0);
    sqlite3_finalize(cs);
    if (sqlite3_prepare_v2(db,
            "SELECT a.id, a.name, COUNT(DISTINCT g.id) FROM artists a"
            " JOIN group_artists ga ON ga.artist_id = a.id"
            " JOIN release_groups g ON g.id = ga.group_id"
            " JOIN releases r ON r.group_id = g.id"
            " JOIN packages p ON p.release_id = r.id"
            " WHERE " VISIBLE
            " GROUP BY a.id, a.name"
            " ORDER BY a.name COLLATE NOCASE"
            " LIMIT ?1 OFFSET ?2", -1, &qs, 0) != SQLITE_OK) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query failed");
    }
    sqlite3_bind_int(qs, 1, limit);
    sqlite3_bind_int(qs, 2, offset);
    while (sqlite3_step(qs) == SQLITE_ROW) {
        mp_json *it = mp_json_obj();
        mp_json_int(it, "id", sqlite3_column_int64(qs, 0));
        mp_json_str(it, "name", col_text(qs, 1));
        mp_json_int(it, "albumCount", sqlite3_column_int64(qs, 2));
        mp_json_add(arr, 0, it);
    }
    sqlite3_finalize(qs);
    mp_json_add(o, "artists", arr);
    mp_json_int(o, "limit", limit);
    mp_json_int(o, "offset", offset);
    mp_json_int(o, "total", total);
    s = mp_json_render(o);
    r = json_response(s, 200);
    free(s);
    mp_json_free(o);
    *st = 200;
    return r;
}

static struct MHD_Response *
handle_artist_detail(mp_library *lib, long long id, unsigned int *st)
{
    sqlite3 *db = mp_library_sqlite(lib);
    sqlite3_stmt *a, *g;
    mp_json *o = mp_json_obj();
    int found = 0;

    if (sqlite3_prepare_v2(db, "SELECT id, name FROM artists WHERE id = ?1",
                           -1, &a, 0) != SQLITE_OK) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query failed");
    }
    sqlite3_bind_int64(a, 1, id);
    if (sqlite3_step(a) == SQLITE_ROW) {
        mp_json_int(o, "id", sqlite3_column_int64(a, 0));
        mp_json_str(o, "name", col_text(a, 1));
        found = 1;
    }
    sqlite3_finalize(a);
    if (!found) {
        mp_json_free(o);
        *st = 404;
        return error_response(404, "not_found", "Artist not found");
    }
    if (sqlite3_prepare_v2(db,
            "SELECT g.id, g.title, g.release_type, g.original_release_date, g.mbid"
            " FROM release_groups g"
            " JOIN group_artists ga ON ga.group_id = g.id"
            " JOIN releases r ON r.group_id = g.id"
            " JOIN packages p ON p.release_id = r.id"
            " WHERE ga.artist_id = ?1 AND " VISIBLE
            " GROUP BY g.id ORDER BY g.title COLLATE NOCASE", -1, &g, 0)
        != SQLITE_OK) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query failed");
    }
    sqlite3_bind_int64(g, 1, id);
    {
        mp_json *alb = mp_json_arr();
        while (sqlite3_step(g) == SQLITE_ROW) {
            mp_json *it = mp_json_obj();
            mp_json_int(it, "id", sqlite3_column_int64(g, 0));
            mp_json_str(it, "title", col_text(g, 1));
            mp_json_str_opt(it, "releaseType", col_text(g, 2));
            mp_json_str_opt(it, "originalReleaseDate", col_text(g, 3));
            mp_json_add(alb, 0, it);
        }
        mp_json_add(o, "albums", alb);
    }
    sqlite3_finalize(g);
    {
        char *s = mp_json_render(o);
        struct MHD_Response *r = json_response(s, 200);
        free(s);
        mp_json_free(o);
        *st = 200;
        return r;
    }
}

static struct MHD_Response *
handle_albums(mp_library *lib, struct MHD_Connection *c, unsigned int *st)
{
    sqlite3 *db = mp_library_sqlite(lib);
    sqlite3_stmt *qs, *cs;
    char *limit_s = (char *) MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, "limit");
    char *offset_s = (char *) MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, "offset");
    int limit, offset, total = 0;
    mp_json *o = mp_json_obj(), *arr = mp_json_arr();
    char *s;
    struct MHD_Response *r;

    if (!parse_paged(limit_s, 50, 1, 200, &limit) ||
        !parse_paged(offset_s, 0, 0, 100000, &offset)) {
        mp_json_free(o);
        *st = 400;
        return error_response(400, "invalid_request",
                              "limit/offset must be non-negative integers");
    }
    if (sqlite3_prepare_v2(db,
            "SELECT COUNT(*) FROM release_groups g WHERE EXISTS ("
            "  SELECT 1 FROM releases r JOIN packages p ON p.release_id = r.id"
            "  WHERE r.group_id = g.id AND " VISIBLE ")", -1, &cs, 0)
        == SQLITE_OK && sqlite3_step(cs) == SQLITE_ROW)
        total = sqlite3_column_int(cs, 0);
    sqlite3_finalize(cs);
    if (sqlite3_prepare_v2(db,
            "SELECT g.id, g.title, g.release_type, g.original_release_date, g.mbid,"
            "  (SELECT a.name FROM group_artists ga"
            "    JOIN artists a ON a.id = ga.artist_id"
            "    WHERE ga.group_id = g.id ORDER BY ga.position LIMIT 1) AS artist,"
            "  (SELECT COUNT(*) FROM releases r WHERE r.group_id = g.id AND"
            "    EXISTS (SELECT 1 FROM packages p WHERE p.release_id = r.id AND "
            "      " VISIBLE ")) AS rc"
            " FROM release_groups g"
            " WHERE EXISTS (SELECT 1 FROM releases r"
            "   JOIN packages p ON p.release_id = r.id"
            "   WHERE r.group_id = g.id AND " VISIBLE ")"
            " ORDER BY artist COLLATE NOCASE, g.title COLLATE NOCASE,"
            "          g.original_release_date, g.id"
            " LIMIT ?1 OFFSET ?2", -1, &qs, 0) != SQLITE_OK) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query failed");
    }
    sqlite3_bind_int(qs, 1, limit);
    sqlite3_bind_int(qs, 2, offset);
    while (sqlite3_step(qs) == SQLITE_ROW) {
        mp_json *it = group_object(lib, qs);
        mp_json_int(it, "releaseCount", sqlite3_column_int64(qs, 6));
        mp_json_add(arr, 0, it);
    }
    sqlite3_finalize(qs);
    mp_json_add(o, "albums", arr);
    mp_json_int(o, "limit", limit);
    mp_json_int(o, "offset", offset);
    mp_json_int(o, "total", total);
    s = mp_json_render(o);
    r = json_response(s, 200);
    free(s);
    mp_json_free(o);
    *st = 200;
    return r;
}

static struct MHD_Response *
handle_album_detail(mp_library *lib, long long id, unsigned int *st)
{
    sqlite3 *db = mp_library_sqlite(lib);
    sqlite3_stmt *g, *rs;
    mp_json *o = mp_json_obj();
    int found = 0;

    if (sqlite3_prepare_v2(db,
            "SELECT g.id, g.title, g.release_type, g.original_release_date, g.mbid"
            " FROM release_groups g WHERE g.id = ?1", -1, &g, 0)
        != SQLITE_OK) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query failed");
    }
    sqlite3_bind_int64(g, 1, id);
    if (sqlite3_step(g) == SQLITE_ROW) {
        mp_json_add(o, "album", group_object(lib, g));
        found = 1;
    }
    sqlite3_finalize(g);
    if (!found) {
        mp_json_free(o);
        *st = 404;
        return error_response(404, "not_found", "Album not found");
    }
    if (sqlite3_prepare_v2(db,
            "SELECT r.id, r.edition, r.release_date, r.country, r.label,"
            "  r.catalogue_number, r.barcode, r.mbid, r.identity_source,"
            "  r.identity_confidence,"
            "  (SELECT COUNT(*) FROM tracks t JOIN media me ON me.id = t.media_id"
            "    WHERE me.release_id = r.id) AS tc,"
            "  p.status, p.verify_status"
            " FROM releases r"
            " JOIN packages p ON p.release_id = r.id"
            " WHERE r.group_id = ?1 AND " VISIBLE
            " ORDER BY r.release_date, r.id", -1, &rs, 0) != SQLITE_OK) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query failed");
    }
    sqlite3_bind_int64(rs, 1, id);
    {
        mp_json *rel = mp_json_arr();
        while (sqlite3_step(rs) == SQLITE_ROW) {
            mp_json *it = mp_json_obj();
            mp_json_int(it, "id", sqlite3_column_int64(rs, 0));
            mp_json_str_opt(it, "edition", col_text(rs, 1));
            mp_json_str_opt(it, "releaseDate", col_text(rs, 2));
            mp_json_str_opt(it, "country", col_text(rs, 3));
            mp_json_str_opt(it, "label", col_text(rs, 4));
            mp_json_str_opt(it, "catalogueNumber", col_text(rs, 5));
            mp_json_str_opt(it, "barcode", col_text(rs, 6));
            mp_json_str_opt(it, "mbid", col_text(rs, 7));
            mp_json_str_opt(it, "identitySource", col_text(rs, 8));
            mp_json_str_opt(it, "identityConfidence", col_text(rs, 9));
            mp_json_int(it, "trackCount", sqlite3_column_int64(rs, 10));
            mp_json_add(it, "media",
                        media_formats_of_release(lib, sqlite3_column_int64(rs, 0)));
            mp_json_str_opt(it, "packageStatus", col_text(rs, 11));
            mp_json_str_opt(it, "verifyStatus", col_text(rs, 12));
            mp_json_add(rel, 0, it);
        }
        mp_json_add(o, "releases", rel);
    }
    sqlite3_finalize(rs);
    {
        char *s = mp_json_render(o);
        struct MHD_Response *r = json_response(s, 200);
        free(s);
        mp_json_free(o);
        *st = 200;
        return r;
    }
}

static mp_json *
track_object(mp_library *lib, sqlite3_stmt *t)
{
    mp_json *o = mp_json_obj();
    char url[64];
    mp_json_int(o, "id", sqlite3_column_int64(t, 0));
    mp_json_int(o, "number", sqlite3_column_int(t, 1));
    mp_json_str(o, "title", col_text(t, 2));
    mp_json_add(o, "artists", artists_of_track(lib, sqlite3_column_int64(t, 0)));
    mp_json_str_opt(o, "isrc", col_text(t, 3));
    if (sqlite3_column_int(t, 4)) {
        mp_json *l = mp_json_obj();
        mp_json_dbl(l, "lufs", sqlite3_column_double(t, 5));
        mp_json_dbl(l, "truePeakDb", sqlite3_column_double(t, 6));
        mp_json_add(o, "loudness", l);
    }
    {
        mp_json *codec = mp_json_obj();
        mp_json_str(codec, "codec", col_text(t, 7));
        mp_json_str(codec, "mimeType", col_text(t, 8));
        if (sqlite3_column_int(t, 9) != 0)
            mp_json_int(codec, "streamVersion", sqlite3_column_int(t, 9));
        if (sqlite3_column_int64(t, 10) != 0)
            mp_json_int(codec, "sampleRate", sqlite3_column_int64(t, 10));
        if (sqlite3_column_int64(t, 11) != 0)
            mp_json_int(codec, "channels", sqlite3_column_int64(t, 11));
        mp_json_add(o, "codec", codec);
    }
    {
        mp_json *audio = mp_json_obj();
        mp_json_int(audio, "id", sqlite3_column_int64(t, 12));
        mp_json_int(audio, "size", sqlite3_column_int64(t, 13));
        mp_json_str_opt(audio, "sha256", col_text(t, 14));
        snprintf(url, sizeof url, "/api/%s/tracks/%lld/audio",
                 API_VERSION, sqlite3_column_int64(t, 0));
        mp_json_str(audio, "url", url);
        mp_json_add(o, "audio", audio);
    }
    return o;
}

static struct MHD_Response *
handle_tracks(mp_library *lib, long long id, unsigned int *st)
{
    sqlite3 *db = mp_library_sqlite(lib);
    sqlite3_stmt *t;
    struct MHD_Response *r;
    mp_json *o;
    char *s;

    if (sqlite3_prepare_v2(db,
            "SELECT t.id, t.track_number, t.title, t.isrc, t.has_loudness,"
            "  t.loudness_lufs, t.loudness_true_peak_db,"
            "  a.codec, a.mime_type, a.stream_version, a.sample_rate, a.channels,"
            "  a.id, a.file_size, a.sha256,"
            "  me.disc_number, g.id, g.title, r.id, r.edition"
            " FROM tracks t"
            " JOIN media me ON me.id = t.media_id"
            " JOIN releases r ON r.id = me.release_id"
            " JOIN release_groups g ON g.id = r.group_id"
            " JOIN audio_objects a ON a.track_id = t.id"
            " JOIN packages p ON p.release_id = r.id"
            " WHERE t.id = ?1 AND " VISIBLE
            " LIMIT 1", -1, &t, 0) != SQLITE_OK) {
        *st = 500;
        return error_response(500, "internal", "query failed");
    }
    sqlite3_bind_int64(t, 1, id);
    if (sqlite3_step(t) != SQLITE_ROW) {
        sqlite3_finalize(t);
        *st = 404;
        return error_response(404, "not_found", "Track not found");
    }
    o = track_object(lib, t);
    {
        mp_json *ctx = mp_json_obj();
        mp_json_int(ctx, "disc", sqlite3_column_int(t, 15));
        mp_json_int(ctx, "albumId", sqlite3_column_int64(t, 16));
        mp_json_str(ctx, "albumTitle", col_text(t, 17));
        mp_json_int(ctx, "releaseId", sqlite3_column_int64(t, 18));
        mp_json_str_opt(ctx, "releaseEdition", col_text(t, 19));
        mp_json_add(o, "context", ctx);
    }
    sqlite3_finalize(t);
    s = mp_json_render(o);
    r = json_response(s, 200);
    free(s);
    mp_json_free(o);
    *st = 200;
    return r;
}

static struct MHD_Response *
handle_release_detail(mp_library *lib, long long id, unsigned int *st)
{
    sqlite3 *db = mp_library_sqlite(lib);
    sqlite3_stmt *r, *m;
    mp_json *o = mp_json_obj();

    if (sqlite3_prepare_v2(db,
            "SELECT r.id, r.edition, r.release_date, r.country, r.label,"
            "  r.catalogue_number, r.barcode, r.mbid, r.identity_source,"
            "  r.identity_confidence, r.source_type, r.source_store,"
            "  r.source_id, r.provenance_tool, r.provenance_tool_version,"
            "  r.notes, g.id, g.title, g.release_type, g.original_release_date,"
            "  g.mbid, p.status, p.verify_status"
            " FROM releases r"
            " JOIN release_groups g ON g.id = r.group_id"
            " JOIN packages p ON p.release_id = r.id"
            " WHERE r.id = ?1 AND " VISIBLE
            " LIMIT 1", -1, &r, 0) != SQLITE_OK) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query failed");
    }
    sqlite3_bind_int64(r, 1, id);
    if (sqlite3_step(r) != SQLITE_ROW) {
        sqlite3_finalize(r);
        mp_json_free(o);
        *st = 404;
        return error_response(404, "not_found", "Release not found");
    }
    mp_json_int(o, "id", sqlite3_column_int64(r, 0));
    mp_json_str_opt(o, "edition", col_text(r, 1));
    mp_json_str_opt(o, "releaseDate", col_text(r, 2));
    mp_json_str_opt(o, "country", col_text(r, 3));
    mp_json_str_opt(o, "label", col_text(r, 4));
    mp_json_str_opt(o, "catalogueNumber", col_text(r, 5));
    mp_json_str_opt(o, "barcode", col_text(r, 6));
    mp_json_str_opt(o, "mbid", col_text(r, 7));
    mp_json_str_opt(o, "identitySource", col_text(r, 8));
    mp_json_str_opt(o, "identityConfidence", col_text(r, 9));
    mp_json_str_opt(o, "sourceType", col_text(r, 10));
    mp_json_str_opt(o, "sourceStore", col_text(r, 11));
    mp_json_str_opt(o, "sourceId", col_text(r, 12));
    mp_json_str_opt(o, "provenanceTool", col_text(r, 13));
    mp_json_str_opt(o, "provenanceToolVersion", col_text(r, 14));
    mp_json_str_opt(o, "notes", col_text(r, 15));
    mp_json_str_opt(o, "packageStatus", col_text(r, 21));
    mp_json_str_opt(o, "verifyStatus", col_text(r, 22));
    {
        mp_json *g = mp_json_obj();
        mp_json_int(g, "id", sqlite3_column_int64(r, 16));
        mp_json_str(g, "title", col_text(r, 17));
        mp_json_str_opt(g, "releaseType", col_text(r, 18));
        mp_json_str_opt(g, "originalReleaseDate", col_text(r, 19));
        mp_json_str_opt(g, "mbid", col_text(r, 20));
        mp_json_add(g, "artists",
                    artists_of_group(lib, sqlite3_column_int64(r, 16)));
        mp_json_add(o, "album", g);
    }
    sqlite3_finalize(r);

    if (sqlite3_prepare_v2(db,
            "SELECT id, disc_number, format, title, position FROM media"
            " WHERE release_id = ?1 ORDER BY position, id", -1, &m, 0)
        != SQLITE_OK) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query failed");
    }
    sqlite3_bind_int64(m, 1, id);
    {
        mp_json *media = mp_json_arr();
        while (sqlite3_step(m) == SQLITE_ROW) {
            sqlite3_stmt *t;
            mp_json *md = mp_json_obj();
            long long media_id = sqlite3_column_int64(m, 0);
            mp_json_int(md, "disc", sqlite3_column_int(m, 1));
            mp_json_str_opt(md, "format", col_text(m, 2));
            mp_json_str_opt(md, "title", col_text(m, 3));
            if (sqlite3_prepare_v2(db,
                    "SELECT t.id, t.track_number, t.title, t.isrc,"
                    "  t.has_loudness, t.loudness_lufs, t.loudness_true_peak_db,"
                    "  a.codec, a.mime_type, a.stream_version, a.sample_rate,"
                    "  a.channels, a.id, a.file_size, a.sha256"
                    " FROM tracks t JOIN audio_objects a ON a.track_id = t.id"
                    " WHERE t.media_id = ?1"
                    " ORDER BY t.track_number, t.id", -1, &t, 0) == SQLITE_OK) {
                mp_json *trs = mp_json_arr();
                sqlite3_bind_int64(t, 1, media_id);
                while (sqlite3_step(t) == SQLITE_ROW)
                    mp_json_add(trs, 0, track_object(lib, t));
                sqlite3_finalize(t);
                mp_json_add(md, "tracks", trs);
            }
            mp_json_add(media, 0, md);
        }
        mp_json_add(o, "media", media);
    }
    sqlite3_finalize(m);

    {
        sqlite3_stmt *a;
        mp_json *art = mp_json_arr(), *other = mp_json_arr();
        if (sqlite3_prepare_v2(db,
                "SELECT a.id, a.kind, a.role, a.mime_type FROM assets a"
                " WHERE a.release_id = ?1 ORDER BY a.id", -1, &a, 0)
            == SQLITE_OK) {
            char url[64];
            sqlite3_bind_int64(a, 1, id);
            while (sqlite3_step(a) == SQLITE_ROW) {
                mp_json *it = mp_json_obj();
                const char *kind = col_text(a, 1);
                mp_json_int(it, "id", sqlite3_column_int64(a, 0));
                mp_json_str(it, "kind", kind);
                mp_json_str_opt(it, "role", col_text(a, 2));
                mp_json_str(it, "mimeType", col_text(a, 3));
                snprintf(url, sizeof url, "/api/%s/assets/%lld",
                         API_VERSION, sqlite3_column_int64(a, 0));
                mp_json_str(it, "url", url);
                if (kind != 0 && strcmp(kind, "artwork") == 0)
                    mp_json_add(art, 0, it);
                else
                    mp_json_add(other, 0, it);
            }
            sqlite3_finalize(a);
        }
        mp_json_add(o, "artwork", art);
        mp_json_add(o, "assets", other);
    }
    {
        char *s = mp_json_render(o);
        struct MHD_Response *r = json_response(s, 200);
        free(s);
        mp_json_free(o);
        *st = 200;
        return r;
    }
}

static struct MHD_Response *
handle_stream(mp_library *lib, struct MHD_Connection *c, long long id,
              int is_audio, unsigned int *st)
{
    mp_object_ref ref;
    int ok = is_audio
        ? mp_library_track_audio(lib, id, &ref)
        : mp_library_asset(lib, id, &ref);
    if (!ok) {
        *st = 404;
        return error_response(404, "not_found",
                              is_audio ? "Track not found" : "Asset not found");
    }
    return serve_object(lib, &ref, c, st);
}

/* ---------- dispatch ----------------------------------------------------- */

struct MHD_Response *
mp_api_handle(mp_library *lib, struct MHD_Connection *c, const char *method,
              const char *url, unsigned int *status_out)
{
    char path[2048];
    char *q;
    long long id;

    *status_out = 200;
    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        *status_out = 405;
        return error_response(405, "unsupported_method",
                              "only GET and HEAD are supported");
    }
    if (url == 0 || strlen(url) >= sizeof path) {
        *status_out = 400;
        return error_response(400, "invalid_request", "path too long");
    }
    snprintf(path, sizeof path, "%s", url);
    q = strchr(path, '?');
    if (q != 0)
        *q = '\0';

    if (strcmp(path, "/api/v1/health") == 0)
        return handle_health(lib);
    if (strcmp(path, "/api/v1/artists") == 0)
        return handle_artists(lib, c, status_out);
    if (strncmp(path, "/api/v1/artists/", 16) == 0) {
        if (!parse_id(path + 16, &id)) {
            *status_out = 400;
            return error_response(400, "invalid_request", "malformed artist id");
        }
        return handle_artist_detail(lib, id, status_out);
    }
    if (strcmp(path, "/api/v1/albums") == 0)
        return handle_albums(lib, c, status_out);
    if (strncmp(path, "/api/v1/albums/", 15) == 0) {
        if (!parse_id(path + 15, &id)) {
            *status_out = 400;
            return error_response(400, "invalid_request", "malformed album id");
        }
        return handle_album_detail(lib, id, status_out);
    }
    if (strncmp(path, "/api/v1/releases/", 17) == 0) {
        if (!parse_id(path + 17, &id)) {
            *status_out = 400;
            return error_response(400, "invalid_request", "malformed release id");
        }
        return handle_release_detail(lib, id, status_out);
    }
    if (strcmp(path, "/api/v1/tracks") == 0) {
        *status_out = 400;
        return error_response(400, "invalid_request",
                              "track list requires an id");
    }
    if (strncmp(path, "/api/v1/tracks/", 15) == 0) {
        char *rest = path + 15;
        char *slash = strchr(rest, '/');
        if (slash != 0) {
            /* /api/v1/tracks/{id}/audio */
            if (strcmp(slash, "/audio") != 0) {
                *status_out = 404;
                return error_response(404, "not_found", "Unknown endpoint");
            }
            *slash = '\0';
            if (!parse_id(rest, &id)) {
                *status_out = 400;
                return error_response(400, "invalid_request",
                                      "malformed track id");
            }
            return handle_stream(lib, c, id, 1, status_out);
        }
        if (!parse_id(rest, &id)) {
            *status_out = 400;
            return error_response(400, "invalid_request",
                                  "malformed track id");
        }
        return handle_tracks(lib, id, status_out);
    }
    if (strncmp(path, "/api/v1/assets/", 15) == 0) {
        if (!parse_id(path + 15, &id)) {
            *status_out = 400;
            return error_response(400, "invalid_request", "malformed asset id");
        }
        return handle_stream(lib, c, id, 0, status_out);
    }

    *status_out = 404;
    return error_response(404, "not_found", "Unknown endpoint");
}
