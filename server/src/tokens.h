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
/// \file tokens.h
/// Opaque API bearer tokens.
///
/// A token is 32 random bytes (256 bits) encoded as `mpk_` + base64url, shown
/// once at creation. Only its SHA-256 hex is persisted; lookup is by hash
/// (indexed) with constant-time comparison, so a database leak does not
/// recover usable tokens.
#ifndef MPSERVER_TOKENS_H_
#define MPSERVER_TOKENS_H_

#include <stddef.h>

#include <musicpack/error.h>

#include "library.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Encoded token length including NUL ("mpk_" + 43 base64url chars).
#define MP_TOKEN_SECRET_MAX 64
/// Token name max length.
#define MP_TOKEN_NAME_MAX 256

typedef struct mp_token_row {
    long long id;
    char name[MP_TOKEN_NAME_MAX];
    char created_at[64];
    char last_used_at[64];
    char expires_at[64];
    char revoked_at[64];
} mp_token_row;

/// Generates a fresh secret (random, not persisted) into \p out.
musicpack_status mp_token_generate(char *out, size_t cap);

/// Computes the stored form: lowercase-hex SHA-256 of the secret.
musicpack_status mp_token_hash(const char *secret, char *hex, size_t cap);

/// Creates a token row for \p name, returns the one-time secret in \p out.
musicpack_status mp_token_create(mp_library *lib, const char *name,
                                 char *out, size_t cap, long long *id_out);

/// Returns 1 and fills \p row when \p secret maps to a valid (non-revoked,
/// non-expired) token. Updates last_used_at on success.
int mp_token_authorize(mp_library *lib, const char *secret,
                       mp_token_row *row);

/// Revokes a token by id. Returns 1 when a row was revoked.
int mp_token_revoke(mp_library *lib, long long id);

/// Iterates all tokens; \p cb is called with each row (return 0 to stop).
typedef int (*mp_token_iter_fn)(void *ctx, const mp_token_row *row);
int mp_token_list(mp_library *lib, mp_token_iter_fn cb, void *ctx);

/// Constant-time-equivalent comparison (for in-process checks).
int mp_token_secret_eq(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_TOKENS_H_ */
