/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see db.h)
*/
#include "db.h"
#include "schema.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

struct mp_db {
    sqlite3 *db;
};

static void
db_err(char *err, size_t errcap, const char *fmt, ...)
{
    va_list ap;
    if (err == 0 || errcap == 0)
        return;
    va_start(ap, fmt);
    vsnprintf(err, errcap, fmt, ap);
    va_end(ap);
}

int
mp_db_open(mp_db **out, const char *path, int writable,
           char *err, size_t errcap)
{
    mp_db *d;
    int flags = writable
        ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
        : SQLITE_OPEN_READONLY;

    if (out == 0 || path == 0)
        return -1;
    *out = 0;
    d = (mp_db *) calloc(1, sizeof *d);
    if (d == 0) {
        db_err(err, errcap, "out of memory");
        return -1;
    }
    if (sqlite3_open_v2(path, &d->db, flags | SQLITE_OPEN_FULLMUTEX, 0)
        != SQLITE_OK) {
        db_err(err, errcap, "cannot open database '%s': %s", path,
               sqlite3_errmsg(d->db));
        sqlite3_close(d->db);
        free(d);
        return -1;
    }
    sqlite3_busy_timeout(d->db, 5000);
    sqlite3_exec(d->db, "PRAGMA foreign_keys=ON;", 0, 0, 0);
    if (writable) {
        sqlite3_exec(d->db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
        sqlite3_exec(d->db, "PRAGMA synchronous=NORMAL;", 0, 0, 0);
        if (mp_db_migrate(d, err, errcap) != 0) {
            sqlite3_close(d->db);
            free(d);
            return -1;
        }
    }
    *out = d;
    return 0;
}

void
mp_db_close(mp_db *db)
{
    if (db == 0)
        return;
    sqlite3_close(db->db);
    free(db);
}

sqlite3 *
mp_db_sqlite(mp_db *db)
{
    return db != 0 ? db->db : 0;
}

int
mp_db_schema_version(mp_db *db)
{
    sqlite3_stmt *st;
    int v = 0;
    if (db == 0 || db->db == 0)
        return 0;
    if (sqlite3_prepare_v2(db->db, "SELECT version FROM schema_version LIMIT 1",
                           -1, &st, 0) != SQLITE_OK)
        return 0;
    if (sqlite3_step(st) == SQLITE_ROW)
        v = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return v;
}

int
mp_db_migrate(mp_db *db, char *err, size_t errcap)
{
    const char *const *migrations = mp_schema_migrations();
    int count = mp_schema_migration_count();
    int current = mp_db_schema_version(db);
    char sql[512];
    int i;

    if (current < 0)
        return -1;
    if (count == 0)
        return 0;
    /* schema_version table must exist before version 1 can be recorded; it is
       created by the version-0 bootstrap below and checked here. */
    {
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(db->db,
                               "SELECT name FROM sqlite_master "
                               "WHERE type='table' AND name='schema_version'",
                               -1, &st, 0) == SQLITE_OK) {
            int exists = sqlite3_step(st) == SQLITE_ROW;
            sqlite3_finalize(st);
            if (!exists) {
                if (sqlite3_exec(db->db,
                                 "CREATE TABLE schema_version ("
                                 "  version INTEGER PRIMARY KEY,"
                                 "  applied_at TEXT NOT NULL);",
                                 0, 0, 0) != SQLITE_OK) {
                    db_err(err, errcap, "cannot create schema_version: %s",
                           sqlite3_errmsg(db->db));
                    return -1;
                }
                if (sqlite3_exec(db->db,
                                 "INSERT INTO schema_version(version, applied_at)"
                                 " VALUES (0, datetime('now'));",
                                 0, 0, 0) != SQLITE_OK)
                    return -1;
                current = 0;
            }
        }
    }

    for (i = current; i < count; i++) {
        sqlite3_exec(db->db, "BEGIN;", 0, 0, 0);
        if (sqlite3_exec(db->db, migrations[i], 0, 0, 0) != SQLITE_OK) {
            db_err(err, errcap, "migration %d failed: %s", i + 1,
                   sqlite3_errmsg(db->db));
            sqlite3_exec(db->db, "ROLLBACK;", 0, 0, 0);
            return -1;
        }
        snprintf(sql, sizeof sql,
                 "UPDATE schema_version SET version=%d, applied_at=datetime('now');",
                 i + 1);
        if (sqlite3_exec(db->db, sql, 0, 0, 0) != SQLITE_OK) {
            db_err(err, errcap, "cannot record migration %d: %s", i + 1,
                   sqlite3_errmsg(db->db));
            sqlite3_exec(db->db, "ROLLBACK;", 0, 0, 0);
            return -1;
        }
        if (sqlite3_exec(db->db, "COMMIT;", 0, 0, 0) != SQLITE_OK) {
            db_err(err, errcap, "cannot commit migration %d: %s", i + 1,
                   sqlite3_errmsg(db->db));
            sqlite3_exec(db->db, "ROLLBACK;", 0, 0, 0);
            return -1;
        }
    }
    return 0;
}
