/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see tokens.h)
*/
#include "tokens.h"
#include "random.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <musicpack/checksum.h>
#include <sqlite3.h>

static const char BASE64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

musicpack_status
mp_token_generate(char *out, size_t cap)
{
    unsigned char raw[32];
    char enc[44];
    size_t i, o = 0;

    if (out == 0 || cap < MP_TOKEN_SECRET_MAX)
        return MUSICPACK_ERR_INVALID;
    if (mp_random_bytes(raw, sizeof raw) != 0)
        return MUSICPACK_ERR_IO;
    /* base64url without padding: 32 bytes -> 43 chars */
    for (i = 0; i < sizeof raw; i += 3) {
        unsigned v = (unsigned) raw[i] << 16;
        int rem = (int) (sizeof raw - i);
        if (rem > 1)
            v |= (unsigned) raw[i + 1] << 8;
        if (rem > 2)
            v |= (unsigned) raw[i + 2];
        enc[o++] = BASE64URL[(v >> 18) & 63];
        enc[o++] = BASE64URL[(v >> 12) & 63];
        if (rem > 1)
            enc[o++] = BASE64URL[(v >> 6) & 63];
        if (rem > 2)
            enc[o++] = BASE64URL[v & 63];
    }
    snprintf(out, cap, "mpk_%.*s", (int) o, enc);
    return MUSICPACK_OK;
}

musicpack_status
mp_token_hash(const char *secret, char *hex, size_t cap)
{
    return musicpack_sha256(secret, strlen(secret), hex, cap);
}

int
mp_token_secret_eq(const char *a, const char *b)
{
    return musicpack_sha256_eq(a, b);
}

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
fill_token_row(sqlite3_stmt *st, mp_token_row *row)
{
    memset(row, 0, sizeof *row);
    row->id = sqlite3_column_int64(st, 0);
    col_cpy(row->name, sizeof row->name, st, 1);
    col_cpy(row->created_at, sizeof row->created_at, st, 2);
    col_cpy(row->last_used_at, sizeof row->last_used_at, st, 3);
    col_cpy(row->expires_at, sizeof row->expires_at, st, 4);
    col_cpy(row->revoked_at, sizeof row->revoked_at, st, 5);
}

musicpack_status
mp_token_create(mp_library *lib, const char *name, char *out, size_t cap,
                long long *id_out)
{
    char secret[MP_TOKEN_SECRET_MAX];
    char hex[MUSICPACK_SHA256_HEX_SIZE];
    sqlite3_stmt *st;
    sqlite3 *db;

    if (lib == 0 || name == 0 || *name == '\0' || out == 0 ||
        strlen(name) >= MP_TOKEN_NAME_MAX)
        return MUSICPACK_ERR_INVALID;
    if (mp_token_generate(secret, sizeof secret) != MUSICPACK_OK)
        return MUSICPACK_ERR_IO;
    if (mp_token_hash(secret, hex, sizeof hex) != MUSICPACK_OK)
        return MUSICPACK_ERR_INVALID;

    db = mp_library_sqlite(lib);
    st = 0;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO tokens(name, token_hash) VALUES (?1, ?2)",
            -1, &st, 0) != SQLITE_OK)
        return MUSICPACK_ERR_IO;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, hex, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return MUSICPACK_ERR_IO;
    }
    sqlite3_finalize(st);
    if (id_out != 0)
        *id_out = sqlite3_last_insert_rowid(db);
    snprintf(out, cap, "%s", secret);
    return MUSICPACK_OK;
}

int
mp_token_authorize(mp_library *lib, const char *secret, mp_token_row *row)
{
    char hex[MUSICPACK_SHA256_HEX_SIZE];
    sqlite3_stmt *st;
    sqlite3 *db;
    int ok = 0;

    if (lib == 0 || secret == 0 || *secret == '\0')
        return 0;
    if (mp_token_hash(secret, hex, sizeof hex) != MUSICPACK_OK)
        return 0;
    db = mp_library_sqlite(lib);
    if (sqlite3_prepare_v2(db,
            "SELECT id, name, created_at, last_used_at, expires_at, revoked_at"
            " FROM tokens WHERE token_hash = ?1", -1, &st, 0) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, hex, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        fill_token_row(st, row);
        ok = 1;
    }
    sqlite3_finalize(st);
    if (!ok)
        return 0;
    if (row->revoked_at[0] != '\0')
        return 0;
    if (row->expires_at[0] != '\0' &&
        strcmp(row->expires_at, "9999-12-31 23:59:59") < 0) {
        /* expiry is stored as an ISO datetime string; a token is expired when
           expires_at < current time. Compare against datetime('now'). */
        sqlite3_stmt *now;
        char nowstr[64];
        if (sqlite3_prepare_v2(db,
                "SELECT datetime('now')", -1, &now, 0) == SQLITE_OK) {
            if (sqlite3_step(now) == SQLITE_ROW)
                snprintf(nowstr, sizeof nowstr, "%s",
                         sqlite3_column_text(now, 0));
            sqlite3_finalize(now);
            if (strcmp(row->expires_at, nowstr) < 0)
                return 0;
        }
    }
    /* best-effort last-used stamping (throttled by the caller if desired) */
    {
        sqlite3_stmt *up;
        if (sqlite3_prepare_v2(db,
                "UPDATE tokens SET last_used_at = datetime('now') WHERE id=?1",
                -1, &up, 0) == SQLITE_OK) {
            sqlite3_bind_int64(up, 1, row->id);
            sqlite3_step(up);
            sqlite3_finalize(up);
        }
    }
    return 1;
}

int
mp_token_revoke(mp_library *lib, long long id)
{
    sqlite3_stmt *st;
    int changed = 0;
    if (lib == 0)
        return 0;
    if (sqlite3_prepare_v2(mp_library_sqlite(lib),
            "UPDATE tokens SET revoked_at = datetime('now') WHERE id = ?1"
            " AND revoked_at IS NULL", -1, &st, 0) != SQLITE_OK)
        return 0;
    sqlite3_bind_int64(st, 1, id);
    if (sqlite3_step(st) == SQLITE_DONE)
        changed = sqlite3_changes(mp_library_sqlite(lib));
    sqlite3_finalize(st);
    return changed;
}

int
mp_token_list(mp_library *lib, mp_token_iter_fn cb, void *ctx)
{
    sqlite3_stmt *st;
    mp_token_row row;
    int rc = 0;
    if (lib == 0 || cb == 0)
        return 0;
    if (sqlite3_prepare_v2(mp_library_sqlite(lib),
            "SELECT id, name, created_at, last_used_at, expires_at, revoked_at"
            " FROM tokens ORDER BY id", -1, &st, 0) != SQLITE_OK)
        return 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        fill_token_row(st, &row);
        if (!cb(ctx, &row)) {
            rc = 0;
            break;
        }
        rc = 1;
    }
    sqlite3_finalize(st);
    return rc;
}
