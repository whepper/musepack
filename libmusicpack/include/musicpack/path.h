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
/// \file path.h
/// Canonical `.mpack` path rules.
///
/// Manifest paths are always '/'-separated and relative to the package root.
/// v1 rules:
///
///  - backslash and NUL/control characters are rejected;
///  - absolute paths (leading '/', drive letters, UNC, URL schemes) are
///    rejected;
///  - `.` and `..` segments are rejected;
///  - empty segments and empty paths are rejected;
///  - length is bounded (MUSICPACK_PATH_MAX).
///
/// musicpack_path_resolve() additionally enforces that the joined path stays
/// inside the package root (realpath containment, symlink escapes rejected).
#ifndef MUSICPACK_PATH_H_
#define MUSICPACK_PATH_H_
#pragma once

#include <stddef.h>

#include <musicpack/error.h>
#include <musicpack/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Maximum accepted manifest path length.
#define MUSICPACK_PATH_MAX 4096

/// Validates a manifest path against the canonical rules above.
///
/// \param rel manifest-relative path
/// \return MUSICPACK_OK or MUSICPACK_ERR_PATH
MUSICPACK_API musicpack_status musicpack_path_validate(const char *rel);

/// Resolves \p rel under \p root to an absolute path with containment check.
///
/// \param root package root directory
/// \param rel  manifest path
/// \param out  caller buffer for the absolute path
/// \param cap  capacity of \p out
MUSICPACK_API musicpack_status musicpack_path_resolve(const char *root,
                                                      const char *rel,
                                                      char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* MUSICPACK_PATH_H_ */
