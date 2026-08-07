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
/// \file db.h
/// SQLite connection + migration lifecycle for the library database.
///
/// WAL mode, foreign keys on, busy timeout. Writable opens apply pending
/// migrations; read-only opens (serve) expect an up-to-date database.
#ifndef MPSERVER_DB_H_
#define MPSERVER_DB_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sqlite3 sqlite3;
typedef struct mp_db mp_db;

/// Opens (and, when \p writable, migrates) the library database at \p path.
///
/// \param out     receives the handle (NULL on failure)
/// \param path    database file path
/// \param writable 1 for read-write (creates + migrates), 0 for read-only
/// \param err     optional error buffer
/// \param errcap  capacity of \p err
/// \return 0 on success, -1 on failure
int mp_db_open(mp_db **out, const char *path, int writable,
               char *err, size_t errcap);

/// Closes the database.
void mp_db_close(mp_db *db);

/// Raw sqlite3 handle (for queries). Do not close it directly.
sqlite3 *mp_db_sqlite(mp_db *db);

/// Current schema version (0 before any migration).
int mp_db_schema_version(mp_db *db);

/// Applies pending migrations up to the current schema in schema.c.
/// Safe to run repeatedly; each migration is one transaction.
///
/// \return 0 on success, -1 on failure (err filled)
int mp_db_migrate(mp_db *db, char *err, size_t errcap);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_DB_H_ */
