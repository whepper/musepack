/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see http.h)
*/
/// \file sessions.h
/// Browser session cookies backed by the existing bearer-token model.
///
/// A session is an opaque 256-bit secret handed to the browser as an
/// HttpOnly + SameSite cookie after the client proves possession of a valid
/// bearer token (POST /api/v1/session). Only the SHA-256 of the session
/// secret is stored, keyed to the underlying token's hash, so a session
/// inherits the token's validity: revoking or expiring the token also
/// invalidates its sessions. Bearer tokens remain the API surface for
/// CLI/native clients; sessions are a thin cookie convenience on top.
#ifndef MPSERVER_SESSIONS_H_
#define MPSERVER_SESSIONS_H_

#include <stddef.h>

#include <musicpack/error.h>

#include "library.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Encoded session secret length including NUL (43 base64url chars).
#define MP_SESSION_SECRET_MAX 64
/// Sliding session lifetime in days (expiry is refreshed on use).
#define MP_SESSION_MAX_AGE_DAYS 30

typedef struct mp_session_row {
    long long id;
    char token_hash[MUSICPACK_SHA256_HEX_SIZE];
    char created_at[64];
    char last_used_at[64];
    char expires_at[64];
} mp_session_row;

/// Generates a fresh opaque session secret (random, not persisted).
musicpack_status mp_session_secret_generate(char *out, size_t cap);

/// Computes the stored form: lowercase-hex SHA-256 of the secret.
musicpack_status mp_session_hash(const char *secret, char *hex, size_t cap);

/// Exchanges a validated bearer token for a new session. The token secret is
/// authorized first; returns 0 (and leaves \p out untouched) when the token
/// is missing or invalid. On success the session secret is returned in
/// \p out and the cookie's Max-Age window starts.
musicpack_status mp_session_create(mp_library *lib, const char *token_secret,
                                   char *out, size_t cap);

/// Returns 1 and fills \p row when \p secret maps to a valid session whose
/// underlying token is still active. Refreshes last_used_at and the sliding
/// expiry on success.
int mp_session_authorize(mp_library *lib, const char *secret,
                         mp_session_row *row);

/// Revokes the session for \p secret (logout). Returns 1 when revoked.
int mp_session_revoke(mp_library *lib, const char *secret);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_SESSIONS_H_ */
