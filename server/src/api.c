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
#include "scanner.h"
#include "sessions.h"
#include "tokens.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <sys/stat.h>
#include <sqlite3.h>
#include <microhttpd.h>

#include <musicpack/musicpack.h>

#ifndef O_NOFOLLOW
# define O_NOFOLLOW 0
#endif

#define API_VERSION "v1"
#define VISIBLE "p.status NOT IN ('invalid','unavailable')"
#define SESSION_COOKIE "musicpack_session"

/* ---------- small parsing helpers -------------------------------------- */

/* Extracts the value of our session cookie from a Cookie header ("a=b; c=d").
   Returns a pointer into \p cookie (not NUL-terminated) and its length, or 0
   when absent. */
static const char *
cookie_value(const char *cookie, const char *name, size_t *len)
{
    const char *p = cookie;

    for (;;) {
        while (*p == ' ' || *p == ';')
            p++;
        if (strncmp(p, name, strlen(name)) != 0) {
            p = strchr(p, ';');
            if (p == 0)
                return 0;
            p++;
            continue;
        }
        p += strlen(name);
        if (*p != '=')
            return 0;
        p++;
        {
            const char *end = strchr(p, ';');
            *len = end != 0 ? (size_t) (end - p) : strlen(p);
            return *len > 0 ? p : 0;
        }
    }
}

/* Strictly extracts the bearer token from a POST /session JSON body
   {"token":"mpk_..."}. The token grammar is constrained (mpk_ + base64url),
   so a tiny parser beats pulling in a JSON dependency to read one field. */
static int
session_token_from_body(const char *body, size_t len, char *out, size_t cap)
{
    const char *p, *end, *v;
    size_t vlen;

    if (body == 0)
        return 0;
    end = body + len;
    p = strstr(body, "\"token\"");
    if (p == 0 || p >= end)
        return 0;
    p += strlen("\"token\"");
    while (p < end && (*p == ' ' || *p == '\t' || *p == ':'))
        p++;
    if (p >= end || *p != '"')
        return 0;
    p++;
    v = p;
    while (p < end && *p != '"')
        p++;
    if (p >= end)
        return 0;
    vlen = (size_t) (p - v);
    if (vlen == 0 || vlen >= cap)
        return 0;
    memcpy(out, v, vlen);
    out[vlen] = '\0';
    return 1;
}

/* Escapes LIKE wildcards so a user query is treated literally. Returns the
   escaped length (or -1 when it does not fit). */
static int
like_escape(const char *src, char *out, size_t cap)
{
    size_t o = 0;
    for (; *src != '\0' && o + 2 < cap; src++) {
        if (*src == '\\' || *src == '%' || *src == '_')
            out[o++] = '\\';
        out[o++] = *src;
    }
    if (*src != '\0')
        return -1;
    out[o] = '\0';
    return (int) o;
}

/* True when session cookies should carry Secure: forced by --secure-cookies
   or when a TLS-terminating reverse proxy signals HTTPS. */
static int
request_is_secure(const mp_config *cfg, struct MHD_Connection *c)
{
    const char *fwd;

    if (cfg->secure_cookies)
        return 1;
    fwd = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "X-Forwarded-Proto");
    return fwd != 0 && strcasecmp(fwd, "https") == 0;
}

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

/* Strong ETag from the manifest sha256 + revalidation cache policy. The
   object URL is stable across rescans but the bytes can change, so content
   is revalidated rather than treated as immutable. */
static void
add_validators(struct MHD_Response *resp, const mp_object_ref *ref)
{
    if (ref->sha256[0] != '\0') {
        char etag[MUSICPACK_SHA256_HEX_SIZE + 3];
        snprintf(etag, sizeof etag, "\"%s\"", ref->sha256);
        MHD_add_response_header(resp, "ETag", etag);
    }
    MHD_add_response_header(resp, "Cache-Control",
                            "private, max-age=0, must-revalidate");
}

static struct MHD_Response *
serve_object(mp_library *lib, const mp_object_ref *ref,
             struct MHD_Connection *c, unsigned int *status_out)
{
    char abs[MUSICPACK_PATH_MAX + 2];
    int fd;
    struct stat st;
    const char *range;
    const char *inm;
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

    /* If-None-Match takes precedence over Range (RFC 9110 §13.1.1). */
    inm = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "If-None-Match");
    if (ref->sha256[0] != '\0' && inm != 0) {
        char etag[MUSICPACK_SHA256_HEX_SIZE + 3];
        snprintf(etag, sizeof etag, "\"%s\"", ref->sha256);
        if (strcmp(inm, etag) == 0) {
            close(fd);
            resp = MHD_create_response_from_buffer(0, 0,
                                                   MHD_RESPMEM_PERSISTENT);
            MHD_add_response_header(resp, "ETag", etag);
            MHD_add_response_header(resp, "Cache-Control",
                                    "private, max-age=0, must-revalidate");
            *status_out = 304;
            return resp;
        }
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
        add_validators(resp, ref);
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
        add_validators(resp, ref);
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
    char *q_s = (char *) MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, "q");
    int limit, offset, total = 0;
    char sql[2048];
    char esc[256];
    int n;
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
    if (q_s != 0 && like_escape(q_s, esc, sizeof esc) < 0) {
        mp_json_free(o);
        *st = 400;
        return error_response(400, "invalid_request", "search query too long");
    }
    n = snprintf(sql, sizeof sql,
        "SELECT COUNT(*) FROM artists a WHERE EXISTS ("
        "  SELECT 1 FROM group_artists ga"
        "  JOIN releases r ON r.group_id = ga.group_id"
        "  JOIN packages p ON p.release_id = r.id"
        "  WHERE ga.artist_id = a.id AND " VISIBLE ")");
    if (q_s != 0)
        n += snprintf(sql + n, sizeof sql - (size_t) n,
                      " AND a.name LIKE '%%' || ?1 || '%%' ESCAPE '\\'");
    if (n >= (int) sizeof sql) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query too long");
    }
    if (sqlite3_prepare_v2(db, sql, -1, &cs, 0) == SQLITE_OK) {
        if (q_s != 0)
            sqlite3_bind_text(cs, 1, esc, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(cs) == SQLITE_ROW)
            total = sqlite3_column_int(cs, 0);
    }
    sqlite3_finalize(cs);
    n = snprintf(sql, sizeof sql,
        "SELECT a.id, a.name, COUNT(DISTINCT g.id) FROM artists a"
        " JOIN group_artists ga ON ga.artist_id = a.id"
        " JOIN release_groups g ON g.id = ga.group_id"
        " JOIN releases r ON r.group_id = g.id"
        " JOIN packages p ON p.release_id = r.id"
        " WHERE " VISIBLE);
    if (q_s != 0)
        n += snprintf(sql + n, sizeof sql - (size_t) n,
                      " AND a.name LIKE '%%' || ?3 || '%%' ESCAPE '\\'");
    n += snprintf(sql + n, sizeof sql - (size_t) n,
        " GROUP BY a.id, a.name ORDER BY a.name COLLATE NOCASE"
        " LIMIT ?1 OFFSET ?2");
    if (n >= (int) sizeof sql) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query too long");
    }
    if (sqlite3_prepare_v2(db, sql, -1, &qs, 0) != SQLITE_OK) {
        MP_LOGI("search prepare err: %s", sqlite3_errmsg(db));
        MP_LOGI("search sql: %s", sql);
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query failed");
    }
    sqlite3_bind_int(qs, 1, limit);
    sqlite3_bind_int(qs, 2, offset);
    if (q_s != 0)
        sqlite3_bind_text(qs, 3, esc, -1, SQLITE_TRANSIENT);
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
    char *q_s = (char *) MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, "q");
    char *sort_s = (char *) MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, "sort");
    int limit, offset, total = 0;
    char sql[4096];
    char esc[512];
    int recent = 0, n;
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
    if (q_s != 0 && like_escape(q_s, esc, sizeof esc) < 0) {
        mp_json_free(o);
        *st = 400;
        return error_response(400, "invalid_request", "search query too long");
    }
    if (sort_s != 0 && strcmp(sort_s, "recent") == 0)
        recent = 1;

    n = snprintf(sql, sizeof sql,
        "SELECT COUNT(*) FROM release_groups g WHERE EXISTS ("
        "  SELECT 1 FROM releases r JOIN packages p ON p.release_id = r.id"
        "  WHERE r.group_id = g.id AND " VISIBLE ")");
    if (q_s != 0)
        n += snprintf(sql + n, sizeof sql - (size_t) n,
            " AND (g.title LIKE '%%' || ?1 || '%%' ESCAPE '\\' OR EXISTS ("
            "  SELECT 1 FROM group_artists ga2"
            "  JOIN artists ar2 ON ar2.id = ga2.artist_id"
            "  WHERE ga2.group_id = g.id AND ar2.name LIKE '%%' || ?1 || '%%'"
            "   ESCAPE '\\'))");
    if (n >= (int) sizeof sql) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query too long");
    }
    if (sqlite3_prepare_v2(db, sql, -1, &cs, 0) == SQLITE_OK) {
        if (q_s != 0)
            sqlite3_bind_text(cs, 1, esc, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(cs) == SQLITE_ROW)
            total = sqlite3_column_int(cs, 0);
    }
    sqlite3_finalize(cs);

    n = snprintf(sql, sizeof sql,
        "SELECT g.id, g.title, g.release_type, g.original_release_date, g.mbid,"
        "  (SELECT a.name FROM group_artists ga"
        "    JOIN artists a ON a.id = ga.artist_id"
        "    WHERE ga.group_id = g.id ORDER BY ga.position LIMIT 1) AS artist,"
        "  (SELECT COUNT(*) FROM releases r WHERE r.group_id = g.id AND"
        "    EXISTS (SELECT 1 FROM packages p WHERE p.release_id = r.id AND "
        "      " VISIBLE ")) AS rc,"
        "  (SELECT aa.id FROM assets aa"
        "    JOIN releases rr ON rr.id = aa.release_id"
        "    JOIN packages pp ON pp.release_id = rr.id"
        "    WHERE rr.group_id = g.id AND aa.kind = 'artwork'"
        "      AND aa.role = 'front'"
        "      AND pp.status NOT IN ('invalid','unavailable')"
        "    ORDER BY rr.release_date, rr.id, aa.id LIMIT 1) AS art_id"
        " FROM release_groups g"
        " WHERE EXISTS (SELECT 1 FROM releases r"
        "   JOIN packages p ON p.release_id = r.id"
        "   WHERE r.group_id = g.id AND " VISIBLE ")");
    if (q_s != 0)
        n += snprintf(sql + n, sizeof sql - (size_t) n,
            " AND (g.title LIKE '%%' || ?3 || '%%' ESCAPE '\\' OR EXISTS ("
            "  SELECT 1 FROM group_artists ga2"
            "  JOIN artists ar2 ON ar2.id = ga2.artist_id"
            "  WHERE ga2.group_id = g.id AND ar2.name LIKE '%%' || ?3 || '%%'"
            "   ESCAPE '\\'))");
    n += snprintf(sql + n, sizeof sql - (size_t) n,
        "%s LIMIT ?1 OFFSET ?2",
        recent ? " ORDER BY g.created_at DESC, g.id DESC"
               : " ORDER BY artist COLLATE NOCASE, g.title COLLATE NOCASE,"
                 "          g.original_release_date, g.id");
    if (n >= (int) sizeof sql) {
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query too long");
    }
    if (sqlite3_prepare_v2(db, sql, -1, &qs, 0) != SQLITE_OK) {
        MP_LOGI("search prepare err: %s", sqlite3_errmsg(db));
        MP_LOGI("search sql: %s", sql);
        mp_json_free(o);
        *st = 500;
        return error_response(500, "internal", "query failed");
    }
    sqlite3_bind_int(qs, 1, limit);
    sqlite3_bind_int(qs, 2, offset);
    if (q_s != 0)
        sqlite3_bind_text(qs, 3, esc, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(qs) == SQLITE_ROW) {
        mp_json *it = group_object(lib, qs);
        mp_json_int(it, "releaseCount", sqlite3_column_int64(qs, 6));
        if (sqlite3_column_int64(qs, 7) > 0) {
            char url[64];
            mp_json *art = mp_json_obj();
            mp_json_int(art, "id", sqlite3_column_int64(qs, 7));
            snprintf(url, sizeof url, "/api/%s/assets/%lld", API_VERSION,
                     sqlite3_column_int64(qs, 7));
            mp_json_str(art, "url", url);
            mp_json_add(it, "artwork", art);
        }
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
            "  p.status, p.verify_status,"
            "  (SELECT aa.id FROM assets aa WHERE aa.release_id = r.id"
            "    AND aa.kind = 'artwork' AND aa.role = 'front'"
            "    ORDER BY aa.id LIMIT 1) AS art_id"
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
            if (sqlite3_column_int64(rs, 13) > 0) {
                char url[64];
                mp_json *art = mp_json_obj();
                mp_json_int(art, "id", sqlite3_column_int64(rs, 13));
                snprintf(url, sizeof url, "/api/%s/assets/%lld", API_VERSION,
                         sqlite3_column_int64(rs, 13));
                mp_json_str(art, "url", url);
                mp_json_add(it, "artwork", art);
            }
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
    if (sqlite3_column_int(t, 15))
        mp_json_dbl(o, "duration", sqlite3_column_double(t, 16));
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
            "  t.has_duration, t.duration,"
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
        mp_json_int(ctx, "disc", sqlite3_column_int(t, 17));
        mp_json_int(ctx, "albumId", sqlite3_column_int64(t, 18));
        mp_json_str(ctx, "albumTitle", col_text(t, 19));
        mp_json_int(ctx, "releaseId", sqlite3_column_int64(t, 20));
        mp_json_str_opt(ctx, "releaseEdition", col_text(t, 21));
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
            "  g.mbid, p.status, p.verify_status,"
            "  r.has_album_loudness, r.album_lufs, r.album_true_peak_db,"
            "  r.loudness_algorithm"
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
    if (sqlite3_column_int(r, 23)) {
        mp_json *l = mp_json_obj();
        mp_json_str_opt(l, "algorithm", col_text(r, 26));
        mp_json_dbl(l, "albumLufs", sqlite3_column_double(r, 24));
        mp_json_dbl(l, "albumTruePeakDb", sqlite3_column_double(r, 25));
        mp_json_add(o, "loudness", l);
    }
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
                    "  a.channels, a.id, a.file_size, a.sha256,"
                    "  t.has_duration, t.duration"
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

/* ---------- library jobs (scan / verify / status) ----------------------- */

static mp_json *
jobs_json(mp_server_ctx *srv)
{
    mp_job_state *j = srv->jobs;
    mp_json *o = mp_json_obj();
    mp_json *scan = mp_json_obj();
    mp_json *verify = mp_json_obj();
    int scan_running = j->running && j->kind == MP_JOB_SCAN;
    int verify_running = j->running && j->kind == MP_JOB_VERIFY;

    mp_json_int(scan, "running", scan_running);
    mp_json_str(scan, "startedAt", j->started_at);
    mp_json_str(scan, "finishedAt", j->finished_at);
    mp_json_int(scan, "packagesScanned", j->packages_scanned);
    mp_json_int(scan, "added", j->added);
    mp_json_int(scan, "updated", j->updated);
    mp_json_int(scan, "removed", j->removed);
    mp_json_int(scan, "invalid", j->invalid);

    mp_json_int(verify, "running", verify_running);
    mp_json_str(verify, "startedAt", j->started_at);
    mp_json_str(verify, "finishedAt", j->finished_at);
    mp_json_int(verify, "packagesVerified", j->verified_total);
    mp_json_int(verify, "passed", j->verified_passed);
    mp_json_int(verify, "warnings", j->verified_warnings);
    mp_json_int(verify, "failed", j->verified_failed);

    mp_json_add(o, "scan", scan);
    mp_json_add(o, "verify", verify);
    return o;
}

static struct MHD_Response *
library_status_response(mp_server_ctx *srv, unsigned int status)
{
    char *s = mp_json_render(jobs_json(srv));
    struct MHD_Response *r = json_response(s, status);
    free(s);
    return r;
}

static struct MHD_Response *
handle_library_scan(mp_server_ctx *srv, unsigned int *st)
{
    if (mp_jobs_start(srv->jobs, srv->cfg, MP_JOB_SCAN) != 0) {
        *st = 409;
        return error_response(409, "scan_already_running",
                              "a scan or verify is already running");
    }
    *st = 202;
    return library_status_response(srv, 202);
}

static struct MHD_Response *
handle_library_verify(mp_server_ctx *srv, unsigned int *st)
{
    if (mp_jobs_start(srv->jobs, srv->cfg, MP_JOB_VERIFY) != 0) {
        *st = 409;
        return error_response(409, "scan_already_running",
                              "a scan or verify is already running");
    }
    *st = 202;
    return library_status_response(srv, 202);
}

static struct MHD_Response *
handle_library_status(mp_server_ctx *srv, unsigned int *st)
{
    *st = 200;
    return library_status_response(srv, 200);
}

/* ---------- session routes (Phase 6) ------------------------------------ */

/* POST /api/v1/session: exchange a validated bearer token for an HttpOnly
   session cookie. The token is used once and never returned again. */
static struct MHD_Response *
handle_session_create(mp_server_ctx *srv, struct MHD_Connection *c,
                      const char *body, size_t body_len, unsigned int *st)
{
    char secret[MP_SESSION_SECRET_MAX];
    char token[MP_TOKEN_SECRET_MAX];
    char setc[320];
    mp_json *o;
    char *s;
    struct MHD_Response *r;

    if (!session_token_from_body(body, body_len, token, sizeof token)) {
        *st = 400;
        return error_response(400, "invalid_request", "malformed session body");
    }
    if (mp_session_create(srv->lib, token, secret, sizeof secret)
        != MUSICPACK_OK) {
        *st = 401;
        return error_response(401, "unauthorized",
                              "invalid or expired token");
    }
    o = mp_json_obj();
    mp_json_str(o, "status", "authenticated");
    s = mp_json_render(o);
    r = json_response(s, 200);
    free(s);
    mp_json_free(o);
    if (r != 0) {
        snprintf(setc, sizeof setc, "%s=%s; HttpOnly; SameSite=Strict;"
                 " Path=/; Max-Age=%d%s", SESSION_COOKIE, secret,
                 MP_SESSION_MAX_AGE_DAYS * 24 * 3600,
                 request_is_secure(srv->cfg, c) ? "; Secure" : "");
        MHD_add_response_header(r, "Set-Cookie", setc);
    }
    *st = 200;
    return r;
}

/* DELETE /api/v1/session: logout. Always clears the cookie (idempotent) and
   revokes the session when the cookie is still valid. Public so an expired
   session can still be cleared. */
static struct MHD_Response *
handle_session_delete(mp_server_ctx *srv, struct MHD_Connection *c,
                      unsigned int *st)
{
    const char *cookie =
        MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Cookie");
    const char *val;
    size_t len = 0;
    struct MHD_Response *r;

    val = cookie != 0 ? cookie_value(cookie, SESSION_COOKIE, &len) : 0;
    if (val != 0 && len < MP_SESSION_SECRET_MAX) {
        char secret[MP_SESSION_SECRET_MAX];
        memcpy(secret, val, len);
        secret[len] = '\0';
        mp_session_revoke(srv->lib, secret);
    }
    r = MHD_create_response_from_buffer(0, 0, MHD_RESPMEM_PERSISTENT);
    if (r != 0) {
        MHD_add_response_header(r, "Set-Cookie",
                                SESSION_COOKIE
                                "=; HttpOnly; SameSite=Strict; Path=/;"
                                " Max-Age=0");
        MHD_add_response_header(r, "Cache-Control", "no-store");
    }
    *st = 204;
    return r;
}

/* GET /api/v1/session: session probe (the request already passed auth).
   Reports the session id when a session cookie was used. */
static struct MHD_Response *
handle_session_get(mp_library *lib, struct MHD_Connection *c,
                   unsigned int *st)
{
    const char *cookie =
        MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Cookie");
    const char *val;
    size_t len = 0;
    mp_json *o = mp_json_obj();
    char *s;
    struct MHD_Response *r;

    val = cookie != 0 ? cookie_value(cookie, SESSION_COOKIE, &len) : 0;
    if (val != 0 && len < MP_SESSION_SECRET_MAX) {
        char secret[MP_SESSION_SECRET_MAX];
        mp_session_row srow;
        memcpy(secret, val, len);
        secret[len] = '\0';
        if (mp_session_authorize(lib, secret, &srow)) {
            mp_json *so = mp_json_obj();
            mp_json_int(so, "id", srow.id);
            mp_json_str(so, "createdAt", srow.created_at);
            mp_json_str(so, "expiresAt", srow.expires_at);
            mp_json_add(o, "session", so);
        }
    }
    mp_json_str(o, "status", "authenticated");
    s = mp_json_render(o);
    r = json_response(s, 200);
    free(s);
    mp_json_free(o);
    *st = 200;
    return r;
}

/* ---------- dispatch ----------------------------------------------------- */

/* Routes the authenticated request. Authentication and CORS live in
   mp_api_handle so route handlers stay auth-agnostic. */
static struct MHD_Response *
dispatch(mp_server_ctx *srv, struct MHD_Connection *c, const char *method,
         const char *url, const char *body, size_t body_len,
         unsigned int *status_out)
{
    mp_library *lib = srv->lib;
    char path[2048];
    char *q;
    long long id;

    *status_out = 200;
    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0 &&
        strcmp(method, "POST") != 0 && strcmp(method, "DELETE") != 0) {
        *status_out = 405;
        return error_response(405, "unsupported_method",
                              "only GET, HEAD, POST and DELETE are supported");
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

    /* ---- session routes: create/logout are public ---------------------- */
    if (strcmp(path, "/api/v1/session") == 0) {
        if (strcmp(method, "POST") == 0) {
            if (body == 0 || body_len == 0) {
                *status_out = 400;
                return error_response(400, "invalid_request",
                                      "a JSON body with the bearer token is "
                                      "required");
            }
            return handle_session_create(srv, c, body, body_len, status_out);
        }
        if (strcmp(method, "DELETE") == 0)
            return handle_session_delete(srv, c, status_out);
        if (strcmp(method, "GET") == 0)
            return handle_session_get(lib, c, status_out);
        *status_out = 405;
        return error_response(405, "unsupported_method",
                              "use POST, GET or DELETE for sessions");
    }

    /* ---- everything else under /api/v1 except health requires a token
       (bearer header) or a valid session cookie ---- */
    {
        const char *auth = MHD_lookup_connection_value(c, MHD_HEADER_KIND,
                                                       "Authorization");
        int ok = 0;
        if (auth != 0 && strncasecmp(auth, "Bearer ", 7) == 0) {
            mp_token_row row;
            ok = mp_token_authorize(lib, auth + 7, &row);
        } else {
            const char *cookie = MHD_lookup_connection_value(c,
                                        MHD_HEADER_KIND, "Cookie");
            const char *val;
            size_t len = 0;
            val = cookie != 0
                ? cookie_value(cookie, SESSION_COOKIE, &len) : 0;
            if (val != 0 && len < MP_SESSION_SECRET_MAX) {
                char secret[MP_SESSION_SECRET_MAX];
                mp_session_row srow;
                memcpy(secret, val, len);
                secret[len] = '\0';
                ok = mp_session_authorize(lib, secret, &srow);
            }
        }
        if (!ok) {
            *status_out = 401;
            return error_response(401, "unauthorized",
                                  "missing, invalid, expired or revoked "
                                  "credentials");
        }
    }

    if (strcmp(path, "/api/v1/library/scan") == 0) {
        if (strcmp(method, "POST") != 0) {
            *status_out = 405;
            return error_response(405, "unsupported_method",
                                  "use POST for library scan");
        }
        return handle_library_scan(srv, status_out);
    }
    if (strcmp(path, "/api/v1/library/verify") == 0) {
        if (strcmp(method, "POST") != 0) {
            *status_out = 405;
            return error_response(405, "unsupported_method",
                                  "use POST for library verify");
        }
        return handle_library_verify(srv, status_out);
    }
    if (strcmp(path, "/api/v1/library/status") == 0)
        return handle_library_status(srv, status_out);

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

/* Public entry: CORS gate, then dispatch. Authentication for the API routes
   happens inside dispatch (health + session create/logout are public). */
struct MHD_Response *
mp_api_handle(mp_server_ctx *srv, struct MHD_Connection *c, const char *method,
              const char *url, const char *body, size_t body_len,
              unsigned int *status_out)
{
    const char *origin = MHD_lookup_connection_value(c, MHD_HEADER_KIND,
                                                     "Origin");
    int cors_ok = 0;
    struct MHD_Response *resp;

    *status_out = 200;
    if (origin != 0) {
        if (mp_config_origin_allowed(srv->cfg, origin)) {
            cors_ok = 1;
        } else {
            *status_out = 403;
            return error_response(403, "origin_forbidden",
                                  "origin not allowed");
        }
    }
    if (strcmp(method, "OPTIONS") == 0) {
        if (!cors_ok) {
            *status_out = 403;
            return error_response(403, "origin_forbidden",
                                  "preflight origin not allowed");
        }
        resp = MHD_create_response_from_buffer(0, 0, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN,
                                origin);
        MHD_add_response_header(resp, "Access-Control-Allow-Methods",
                                "GET, HEAD, POST, DELETE, OPTIONS");
        MHD_add_response_header(resp, "Access-Control-Allow-Headers",
                                "Authorization, Content-Type");
        MHD_add_response_header(resp, "Access-Control-Allow-Credentials",
                                "true");
        MHD_add_response_header(resp, "Access-Control-Max-Age", "600");
        MHD_add_response_header(resp, "Vary", "Origin");
        *status_out = 204;
        return resp;
    }
    resp = dispatch(srv, c, method, url, body, body_len, status_out);
    if (resp != 0 && cors_ok) {
        MHD_add_response_header(resp,
                                MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN,
                                origin);
        MHD_add_response_header(resp, "Access-Control-Allow-Credentials",
                                "true");
        MHD_add_response_header(resp, "Vary", "Origin");
    }
    return resp;
}
