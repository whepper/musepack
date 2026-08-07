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
/// \file package.h
/// An opened `.mpack` package over a storage backend.
///
/// Phase 2 implements the directory backend. The logical album model
/// (musicpack_manifest) is independent of storage, so a future packed MPAK
/// backend plugs in behind this same handle.
///
/// The package is treated as untrusted input: manifest parsing applies the
/// canonical path/security rules and verification re-checks hashes and file
/// presence.
#ifndef MUSICPACK_PACKAGE_H_
#define MUSICPACK_PACKAGE_H_
#pragma once

#include <stddef.h>

#include <musicpack/error.h>
#include <musicpack/export.h>
#include <musicpack/manifest.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque package handle.
typedef struct musicpack_package musicpack_package;

/// Verification outcome counts. Errors are failures; warnings are noted but
/// non-fatal (e.g. unreferenced files present in the directory).
typedef struct musicpack_report {
    size_t errors;
    size_t warnings;
} musicpack_report;

/// Optional verification message callback (one call per finding).
/// \p is_error distinguishes failures from warnings.
typedef void (*musicpack_report_fn)(void *ctx, const char *message, int is_error);

/// Opens a directory-form package: reads and structurally validates
/// `manifest.json` at the root of \p dir.
///
/// \param dir    package directory (e.g. "Album.mpack")
/// \param status optional error out
/// \return an owned handle, or NULL on failure
MUSICPACK_API musicpack_package *musicpack_package_open_dir(const char *dir,
                                                            musicpack_status *status);

/// Closes a package and releases all state.
MUSICPACK_API void musicpack_package_close(musicpack_package *pkg);

/// The package's parsed manifest (owned by the package).
MUSICPACK_API const musicpack_manifest *musicpack_package_manifest(const musicpack_package *pkg);

/// Verifies the package: referenced files exist, sha256 values match, and
/// structural invariants hold (path rules were already enforced at parse).
///
/// \param pkg       package to verify
/// \param rep       optional; receives error/warning counts
/// \param report    optional per-finding callback (may be NULL)
/// \param report_ctx opaque context passed to \p report
/// \return MUSICPACK_OK when there are no errors (warnings allowed),
///         MUSICPACK_ERR_* otherwise
MUSICPACK_API musicpack_status musicpack_package_verify(const musicpack_package *pkg,
                                                        musicpack_report *rep,
                                                        musicpack_report_fn report,
                                                        void *report_ctx);

/// Rewrites the package's manifest.json, preserving any unknown fields that
/// were present when the package was opened (forward-compatible round-trip).
///
/// \param pkg package to save
/// \return MUSICPACK_OK or an error
MUSICPACK_API musicpack_status musicpack_package_save_manifest(const musicpack_package *pkg);

#ifdef __cplusplus
}
#endif
#endif /* MUSICPACK_PACKAGE_H_ */
