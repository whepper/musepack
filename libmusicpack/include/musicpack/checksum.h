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
/// \file checksum.h
/// SHA-256 integrity helpers.
#ifndef MUSICPACK_CHECKSUM_H_
#define MUSICPACK_CHECKSUM_H_
#pragma once

#include <stddef.h>

#include <musicpack/error.h>
#include <musicpack/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/// SHA-256 hex digest length in characters (plus NUL).
#define MUSICPACK_SHA256_HEX_SIZE 65

/// Computes the lowercase-hex SHA-256 of a byte buffer.
///
/// \param data input
/// \param len  input length
/// \param hex  caller buffer of at least MUSICPACK_SHA256_HEX_SIZE
/// \param cap  capacity of \p hex
MUSICPACK_API musicpack_status musicpack_sha256(const void *data, size_t len,
                                                char *hex, size_t cap);

/// Computes the lowercase-hex SHA-256 of a file's contents.
MUSICPACK_API musicpack_status musicpack_sha256_file(const char *path,
                                                     char *hex, size_t cap);

/// Constant-time-equivalent comparison of two lowercase-hex digests.
/// Returns 1 if equal, 0 otherwise.
MUSICPACK_API int musicpack_sha256_eq(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
#endif /* MUSICPACK_CHECKSUM_H_ */
