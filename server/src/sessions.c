/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see sessions.h)
*/
#include "sessions.h"
#include "random.h"
#include "tokens.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <musicpack/checksum.h>
#include <sqlite3.h>

static const char BASE64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

musicpack_status
mp_session_secret_generate(char *out, size_t cap)
{
    unsigned char raw[32];
    char enc[44];
    size_t i, o = 0;

    if (out == 0 || cap < MP_SESSION_SECRET_MAX)
        return MUSICPACK_ERR_INVALID;
    if (mp_random_bytes(raw, sizeof raw) != 0)
        return MUSICPACK_ERR_IO;
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
    snprintf(out, cap, "%.*s", (int) o, enc);
    return MUSICPACK_OK;
}

musicpack_status
mp_session_hash(const char *secret, char *hex, size_t cap)
{
    return musicpack_sha256(secret, strlen(secret), hex, cap);
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

musicpack_status
mp_session_create(mp_library *lib, const char *token_secret,
                  char *out, size_t cap)
{
    mp_token_row token;
    char session_secret[MP_SESSION_SECRET_MAX];
    char session_hex[MUSICPACK_SHA256_HEX_SIZE];
    char token_hex[MUSICPACK_SHA256_HEX_SIZE];
    sqlite3_stmt *st;
    sqlite3 *db;

    if (lib == 0 || token_secret == 0 || out == 0 ||
        cap < MP_SESSION_SECRET_MAX)
        return MUSICPACK_ERR_INVALID;
    /* The exchange only succeeds for a currently-valid token. */
    if (!mp_token_authorize(lib, token_secret, &token))
        return MUSICPACK_ERR_INVALID;
    if (mp_session_secret_generate(session_secret, sizeof session_secret)
        != MUSICPACK_OK)
        return MUSICPACK_ERR_IO;
    if (mp_session_hash(session_secret, session_hex, sizeof session_hex)
        != MUSICPACK_OK)
        return MUSICPACK_ERR_INVALID;
    if (mp_token_hash(token_secret, token_hex, sizeof token_hex)
        != MUSICPACK_OK)
        return MUSICPACK_ERR_INVALID;

    db = mp_library_sqlite(lib);
    if (sqlite3_prepare_v2(db,
            "INSERT INTO sessions(session_hash, token_hash, expires_at)"
            " VALUES (?1, ?2, datetime('now', ?3))", -1, &st, 0) != SQLITE_OK)
        return MUSICPACK_ERR_IO;
    sqlite3_bind_text(st, 1, session_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, token_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, "+30 days", -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return MUSICPACK_ERR_IO;
    }
    sqlite3_finalize(st);
    snprintf(out, cap, "%s", session_secret);
    return MUSICPACK_OK;
}

int
mp_session_authorize(mp_library *lib, const char *secret, mp_session_row *row)
{
    char hex[MUSICPACK_SHA256_HEX_SIZE];
    sqlite3_stmt *st;
    sqlite3 *db;
    int ok = 0;

    if (lib == 0 || secret == 0 || *secret == '\0')
        return 0;
    if (mp_session_hash(secret, hex, sizeof hex) != MUSICPACK_OK)
        return 0;
    db = mp_library_sqlite(lib);
    /* A session is only valid while its token is active (revoking or expiring
       the token invalidates every session issued from it). */
    if (sqlite3_prepare_v2(db,
            "SELECT s.id, s.token_hash, s.created_at, s.last_used_at,"
            "  s.expires_at"
            " FROM sessions s JOIN tokens t ON t.token_hash = s.token_hash"
            " WHERE s.session_hash = ?1 AND s.revoked_at IS NULL"
            "  AND t.revoked_at IS NULL", -1, &st, 0) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, hex, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        memset(row, 0, sizeof *row);
        row->id = sqlite3_column_int64(st, 0);
        col_cpy(row->token_hash, sizeof row->token_hash, st, 1);
        col_cpy(row->created_at, sizeof row->created_at, st, 2);
        col_cpy(row->last_used_at, sizeof row->last_used_at, st, 3);
        col_cpy(row->expires_at, sizeof row->expires_at, st, 4);
        ok = 1;
    }
    sqlite3_finalize(st);
    if (!ok)
        return 0;
    if (row->expires_at[0] != '\0') {
        sqlite3_stmt *now;
        char nowstr[64];
        if (sqlite3_prepare_v2(db, "SELECT datetime('now')", -1, &now, 0)
            == SQLITE_OK) {
            if (sqlite3_step(now) == SQLITE_ROW)
                snprintf(nowstr, sizeof nowstr, "%s",
                         sqlite3_column_text(now, 0));
            sqlite3_finalize(now);
            if (strcmp(row->expires_at, nowstr) < 0)
                return 0;
        }
    }
    /* sliding expiry + last-used stamp */
    {
        sqlite3_stmt *up;
        if (sqlite3_prepare_v2(db,
                "UPDATE sessions SET last_used_at = datetime('now'),"
                "  expires_at = datetime('now', ?1) WHERE id = ?2",
                -1, &up, 0) == SQLITE_OK) {
            sqlite3_bind_text(up, 1, "+30 days", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(up, 2, row->id);
            sqlite3_step(up);
            sqlite3_finalize(up);
        }
    }
    return 1;
}

int
mp_session_revoke(mp_library *lib, const char *secret)
{
    char hex[MUSICPACK_SHA256_HEX_SIZE];
    sqlite3_stmt *st;
    int changed = 0;

    if (lib == 0 || secret == 0 || *secret == '\0')
        return 0;
    if (mp_session_hash(secret, hex, sizeof hex) != MUSICPACK_OK)
        return 0;
    if (sqlite3_prepare_v2(mp_library_sqlite(lib),
            "UPDATE sessions SET revoked_at = datetime('now')"
            " WHERE session_hash = ?1 AND revoked_at IS NULL", -1, &st, 0)
        != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, hex, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_DONE)
        changed = sqlite3_changes(mp_library_sqlite(lib));
    sqlite3_finalize(st);
    return changed;
}
