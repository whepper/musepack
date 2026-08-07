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
/// \file identity.h
/// Package / release-group / release identity.
///
/// Filesystem paths are NOT the conceptual identity of a release. Three
/// levels are computed server-side (nothing is added to .mpack v1):
///
///  - package fingerprint: sha256 of the canonical manifest serialization.
///    Any content change (including audio sha256 in the manifest) yields a
///    new fingerprint; a moved package keeps its fingerprint, so a package
///    reappearing at a new path is recognised as the same package.
///  - group key: MusicBrainz release-group id when present, else a hash of
///    the stable group fields (title, sorted artists, original date, type).
///    Distinct editions group into the same release group.
///  - release key: MusicBrainz release id when present, else a hash of the
///    edition-distinguishing fields (edition, date, country, label,
///    catalogue, barcode) within the group.
#ifndef MPSERVER_IDENTITY_H_
#define MPSERVER_IDENTITY_H_

#include <stddef.h>

#include <musicpack/musicpack.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Identifier string capacity (64 hex chars + prefix + NUL).
#define MP_ID_KEY_MAX 96

/// sha256 of \p json bytes (hex, MUSICPACK_SHA256_HEX_SIZE). Used for
/// cheap change detection on the manifest file itself.
musicpack_status mp_identity_manifest_hash(const char *json, size_t len,
                                           char *out, size_t cap);

/// Package fingerprint: sha256 of the canonical manifest serialization.
musicpack_status mp_identity_package_fingerprint(const musicpack_manifest *m,
                                                 char *out, size_t cap);

/// Stable release-group key (see file docs).
musicpack_status mp_identity_group_key(const musicpack_manifest *m,
                                       char *out, size_t cap);

/// Stable release/edition key within a group (see file docs).
musicpack_status mp_identity_release_key(const musicpack_manifest *m,
                                         char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_IDENTITY_H_ */
