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
/// \file scanner.h
/// Library scanner: finds `.mpack` directory bundles under a root, validates
/// them through libmusicpack, and upserts the collector library.
///
/// Behavior:
///  - recursion stops at a `.mpack` directory (the package root);
///  - symlinks are never followed during traversal;
///  - the manifest file's sha256 is the cheap change detector (unchanged
///    packages are skipped without reopening);
///  - a package reappearing at a new path with the same content fingerprint
///    is a move, not a new album;
///  - each package is ingested in its own transaction, so a malformed
///    package never corrupts the index;
///  - packages absent from the filesystem are marked unavailable.
#ifndef MPSERVER_SCANNER_H_
#define MPSERVER_SCANNER_H_

#include <musicpack/error.h>

#include "library.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mp_scan_result {
    int added;      ///< packages newly indexed
    int updated;    ///< packages whose content changed in place
    int moved;      ///< packages recognised at a new path
    int removed;    ///< packages marked unavailable this scan
    int invalid;    ///< packages whose manifest could not be parsed
    int total;      ///< packages seen (valid + invalid)
} mp_scan_result;

/// Scans \p root into \p lib.
///
/// \param verify 1 = run full integrity verification (sha256 of every
///        referenced object) and record statuses; 0 = manifest + object
///        existence only.
musicpack_status mp_scan_library(mp_library *lib, const char *root,
                                 int verify, mp_scan_result *res);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_SCANNER_H_ */
