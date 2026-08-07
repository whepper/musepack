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
/// \file object.h
/// Audio object access and the Musepack handoff.
///
/// The long-term path is:
///
///     MusicPack audio object
///             ↓
///     codec-specific backend
///
/// Phase 2 provides the directory backend plus a Musepack backend adapter
/// that fills the existing `mpc_reader` (from <mpc/reader.h>, libmusepack)
/// over a track's `.mpc`, so the contained audio is decoded without copying
/// or reconstructing it.
#ifndef MUSICPACK_OBJECT_H_
#define MUSICPACK_OBJECT_H_
#pragma once

#include <stddef.h>

#include <mpc/reader.h>

#include <musicpack/error.h>
#include <musicpack/export.h>
#include <musicpack/package.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Resolves a manifest-relative path to an absolute filesystem path inside
/// the package, enforcing containment (no `..`, no absolute paths, no
/// symlink escapes). The result is suitable for direct file access.
///
/// \param pkg package
/// \param rel manifest-relative path
/// \param out receives the absolute path (caller buffer)
/// \param cap capacity of \p out
/// \return MUSICPACK_OK, MUSICPACK_ERR_PATH on traversal/escape, or other error
MUSICPACK_API musicpack_status musicpack_package_resolve_path(const musicpack_package *pkg,
                                                              const char *rel,
                                                              char *out, size_t cap);

/// Convenience: absolute path of a track's audio object.
///
/// \param pkg   package
/// \param disc  0-based disc index
/// \param track 0-based track index within the disc
/// \param out,cap caller buffer
MUSICPACK_API musicpack_status musicpack_package_track_path(const musicpack_package *pkg,
                                                            size_t disc, size_t track,
                                                            char *out, size_t cap);

/// Opens a track's audio object as an `mpc_reader` (Musepack backend).
///
/// The returned reader is a stdio reader over the contained file; the caller
/// closes it with mpc_reader_exit_stdio(). Only meaningful for Musepack
/// audio objects, but the path is identical for any codec that consumes
/// `mpc_reader`.
///
/// \param pkg    package
/// \param disc   0-based disc index
/// \param track  0-based track index
/// \param reader receives an initialized mpc_reader
/// \return MUSICPACK_OK or an error
MUSICPACK_API musicpack_status musicpack_package_track_open_reader(const musicpack_package *pkg,
                                                                   size_t disc, size_t track,
                                                                   mpc_reader *reader);

#ifdef __cplusplus
}
#endif
#endif /* MUSICPACK_OBJECT_H_ */
