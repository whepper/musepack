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
/// \file manifest.h
/// The `.mpack` v1 manifest model: what an album IS (independent of storage).
///
/// The model is a plain, owned C structure. All string members are
/// `strdup`'d and released by musicpack_manifest_free(). Unknown fields in
/// the source JSON are not represented here; they are preserved on write by
/// passing the original parse tree (see musicpack_manifest_write()).
#ifndef MUSICPACK_MANIFEST_H_
#define MUSICPACK_MANIFEST_H_
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <musicpack/error.h>
#include <musicpack/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Manifes format identifier.
#define MUSICPACK_FORMAT "musicpack"
/// Current schema version.
#define MUSICPACK_VERSION_SCHEMA 1

/// A named credit (artist/author/role).
typedef struct musicpack_artist {
    char *name; ///< required
    char *role; ///< optional ("main", "featuring", ...), may be NULL
} musicpack_artist;

/// A referenced object in the package (manifest-relative path + optional hash).
typedef struct musicpack_asset {
    char *path;  ///< required, '/'-separated, relative, contained
    char *sha256; ///< optional lowercase hex; may be NULL
} musicpack_asset;

/// Measured BS.1770 loudness (gain is derived, never stored).
typedef struct musicpack_loudness {
    int present;
    double lufs;        ///< integrated loudness, dB LUFS
    double true_peak_db; ///< true peak, dBTP
} musicpack_loudness;

/// A single track on a disc.
typedef struct musicpack_track {
    int number;
    char *title;
    musicpack_artist *artists; ///< optional per-track override; may be NULL
    size_t artist_count;
    char *isrc;
    char *source_store;   ///< optional provenance (e.g. "Deezer")
    char *source_track_id; ///< optional provider track id
    char *source_audio_codec; ///< optional pre-encoding codec ("flac")
    char *source_audio_md5;   ///< optional pre-encoding source hash
    int has_duration;     ///< duration is DERIVED, not canonical
    double duration;      ///< seconds
    musicpack_loudness loudness;
    musicpack_asset audio;
} musicpack_track;

/// A disc / medium.
typedef struct musicpack_disc {
    int disc;
    char *title; ///< optional; may be NULL
    musicpack_track *tracks;
    size_t track_count;
} musicpack_disc;

/// Artwork with a role tag ("front", "back", "disc", ...).
typedef struct musicpack_artwork {
    char *role;
    musicpack_asset asset;
} musicpack_artwork;

/// The parsed v1 manifest.
typedef struct musicpack_manifest {
    char *album_title;
    musicpack_artist *album_artists;
    size_t album_artist_count;
    char *release_date;    ///< optional ISO-8601
    char **genres;         ///< optional multi-value
    size_t genre_count;
    char *musicbrainz_release_id; ///< optional
    char *barcode;                ///< optional
    char *identity_source;        ///< optional: musicbrainz|store|local
    char *identity_confidence;    ///< optional: exact|confirmed|probable|none
    char *source_type;            ///< optional: cd-rip|digital-download|...
    char *source_store;           ///< optional
    char *source_id;              ///< optional provider release id
    musicpack_disc *discs;
    size_t disc_count;
    musicpack_artwork *artwork;
    size_t artwork_count;
    musicpack_asset *booklet;
    size_t booklet_count;
    musicpack_asset *lyrics;
    size_t lyrics_count;
    musicpack_asset *extras;
    size_t extras_count;
    int has_album_loudness;
    musicpack_loudness album_loudness;
    char *provenance_tool;       ///< optional
    char *provenance_tool_version; ///< optional
} musicpack_manifest;

/// Parses manifest JSON text into a typed model (validation is structural:
/// format, version, required fields, path rules, numbering, loudness ranges).
///
/// \param json      NUL-terminated JSON text
/// \param status    optional error out
/// \return an owned model, or NULL on failure
MUSICPACK_API musicpack_manifest *musicpack_manifest_parse(const char *json,
                                                           musicpack_status *status);

/// Releases a manifest and all owned strings.
MUSICPACK_API void musicpack_manifest_free(musicpack_manifest *m);

/// Serializes a manifest to JSON text in canonical key order.
///
/// \param m        manifest to serialize
/// \param json_out receives a NUL-terminated string (caller frees)
/// \return MUSICPACK_OK or an error
MUSICPACK_API musicpack_status musicpack_manifest_write(const musicpack_manifest *m,
                                                        char **json_out);

#ifdef __cplusplus
}
#endif
#endif /* MUSICPACK_MANIFEST_H_ */
