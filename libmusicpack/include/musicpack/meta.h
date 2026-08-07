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
/// \file meta.h
/// Audio-file metadata model and readers (Phase 3A: local ingestion).
///
/// A single ordered, multi-value tag bag (musicpack_tag_set) is the
/// interchange format for both Vorbis Comments and APEv2. Tag data is
/// untrusted input: values are bounded, UTF-8 validated, embedded NULs are
/// truncated, and binary items (e.g. FLAC PICTURE / APEv2 cover art) are
/// size-capped. This module is read-only in Phase 3A.
#ifndef MUSICPACK_META_H_
#define MUSICPACK_META_H_
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <musicpack/error.h>
#include <musicpack/export.h>
#include <musicpack/manifest.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bounds for untrusted tag/artwork input. */
#define MUSICPACK_TAG_KEY_MAX     256
#define MUSICPACK_TAG_VALUE_MAX   (1u * 1024u * 1024u)
#define MUSICPACK_TAG_COUNT_MAX   4096
#define MUSICPACK_PICTURE_MAX     (32u * 1024u * 1024u)
#define MUSICPACK_METADATA_BLOCK_MAX (64u * 1024u * 1024u)

/// A single tag item: either a UTF-8 text value or a raw binary payload.
typedef struct musicpack_tag {
    char *key;          ///< non-empty, NUL-terminated
    char *value;        ///< UTF-8 text, NUL-terminated; NULL when is_binary
    size_t value_len;   ///< byte length of value (excluding terminator)
    int is_binary;      ///< 1 when the item carries binary data
    size_t binary_len;  ///< byte length of binary payload (when is_binary)
    unsigned char *binary;
} musicpack_tag;

/// An ordered multi-value tag set (repeated keys = legitimate multi-value).
typedef struct musicpack_tag_set {
    musicpack_tag *items;
    size_t count;
    size_t cap;
    char *source;       ///< e.g. "vorbis-comment", "apev2", "flac"
} musicpack_tag_set;

/// A FLAC PICTURE metadata block (raw bytes preserved, never decoded).
typedef struct musicpack_picture {
    int type;           ///< FLAC picture type (3 = front, 4 = back, ...)
    char *mime;         ///< e.g. "image/jpeg"
    char *description;  ///< UTF-8, may be empty
    int width;          ///< pixels, 0 when unknown
    int height;         ///< pixels, 0 when unknown
    int depth;          ///< bits per pixel, 0 when unknown
    int colors;         ///< indexed colors, 0 when unknown
    size_t data_len;    ///< byte length of the image data
    unsigned char *data;
} musicpack_picture;

/// A list of pictures.
typedef struct musicpack_pictures {
    musicpack_picture *items;
    size_t count;
    size_t cap;
} musicpack_pictures;

/// Initializes an empty tag set (does not free a previously owned set).
MUSICPACK_API musicpack_status musicpack_tag_set_init(musicpack_tag_set *s,
                                                      const char *source);

/// Releases a tag set and all owned items.
MUSICPACK_API void musicpack_tag_set_free(musicpack_tag_set *s);

/// Adds a text tag. The value is copied; embedded NULs truncate it. Returns
/// MUSICPACK_ERR_INVALID when the key is invalid, the value exceeds bounds,
/// or the value is not valid UTF-8.
MUSICPACK_API musicpack_status musicpack_tag_set_add(musicpack_tag_set *s,
                                                     const char *key,
                                                     const char *value,
                                                     size_t value_len);

/// Adds a raw binary item (e.g. embedded cover art).
MUSICPACK_API musicpack_status musicpack_tag_set_add_binary(musicpack_tag_set *s,
                                                            const char *key,
                                                            const unsigned char *data,
                                                            size_t len);

/// Returns the first item with \p key (ASCII case-insensitive), or NULL.
MUSICPACK_API const musicpack_tag *musicpack_tag_set_get(const musicpack_tag_set *s,
                                                         const char *key);

/// Writes up to \p cap pointers to all items matching \p key into \p out;
/// returns the number written.
MUSICPACK_API size_t musicpack_tag_set_get_all(const musicpack_tag_set *s,
                                               const char *key,
                                               const musicpack_tag **out,
                                               size_t cap);

/// Returns 1 when bytes [s, s+len) are valid UTF-8.
MUSICPACK_API int musicpack_utf8_valid(const unsigned char *s, size_t len);

/// Releases a pictures list and all owned items.
MUSICPACK_API void musicpack_pictures_free(musicpack_pictures *p);

/// Parses an in-memory Vorbis Comment structure (as stored in FLAC's
/// VORBIS_COMMENT block, without the Ogg framing bit). \p out must be
/// initialized first.
MUSICPACK_API musicpack_status musicpack_vorbis_parse(const unsigned char *data,
                                                      size_t len,
                                                      musicpack_tag_set *out);

/// Reads a file containing a raw Vorbis Comment structure into \p out.
MUSICPACK_API musicpack_status musicpack_vorbis_read(const char *path,
                                                     musicpack_tag_set *out);

/// Reads the APEv2 tag from the end of \p path into \p out. Returns
/// MUSICPACK_OK with an empty set when the file has no APE tag. Handles
/// versions 1.000 and 2.000, header+footer and footer-only tags, text
/// (multi-value NUL-joined), binary and read-only items.
MUSICPACK_API musicpack_status musicpack_ape_read(const char *path,
                                                  musicpack_tag_set *out);

/// Writes (or replaces) the APEv2 tag on \p path: the existing tag, if any,
/// is removed and a header+footer v2.000 tag is appended, preserving the
/// audio bytes. Repeated keys are stored as NUL-joined text values; binary
/// items are stored as binary items. An empty \p tags removes any existing
/// tag. The file is only modified after the tag has been fully built.
MUSICPACK_API musicpack_status musicpack_ape_write(const char *path,
                                                   const musicpack_tag_set *tags);

/// Reads the metadata blocks of a FLAC file: the VORBIS_COMMENT block into
/// \p comments and every PICTURE block into \p pictures. Either may be NULL.
/// Only metadata is read; audio frames are not touched.
MUSICPACK_API musicpack_status musicpack_flac_read_metadata(const char *path,
                                                            musicpack_tag_set *comments,
                                                            musicpack_pictures *pictures);

/* ---- tag -> manifest mapping (Phase 3A: mapping core) ---------------- */

/// Maps album/release/identifier/source fields from a tag set (Vorbis or
/// APEv2) into a manifest. First-wins: existing values are never
/// overwritten, so the first file's album metadata wins. \p m should be
/// zeroed; any strings stored become owned by the manifest.
MUSICPACK_API musicpack_status musicpack_tag_map_album(const musicpack_tag_set *tags,
                                                       musicpack_manifest *m);

/// Maps per-track fields (title, number, artists with roles, ISRC,
/// MusicBrainz IDs, source store/id) from a tag set into a track.
/// First-wins per field.
MUSICPACK_API musicpack_status musicpack_tag_map_track(const musicpack_tag_set *tags,
                                                       musicpack_track *t);

/// Projects manifest + track metadata into an APEv2-ready tag set (the
/// portable audio-file metadata written to a transcoded .mpc). \p out is
/// initialized fresh. \p disc/\p disc_total/\p track_total are optional
/// (0 omits the total).
MUSICPACK_API musicpack_status musicpack_manifest_to_ape_tags(const musicpack_manifest *m,
                                                              const musicpack_track *t,
                                                              int disc, int disc_total,
                                                              int track_total,
                                                              musicpack_tag_set *out);

/// Maps a MusicBrainz release-type vocabulary value (e.g. "album", "live",
/// "remix") to the .mpack v1 releaseType enum ("other" for anything
/// unrecognized). Returns NULL for NULL/empty input.
MUSICPACK_API const char *musicpack_meta_release_type_from_tag(const char *value);

/// Parses "3", "3/12" or "03" into \p out (values >= 1). Returns 1 on
/// success, 0 otherwise.
MUSICPACK_API int musicpack_meta_parse_track_number(const char *value, int *out);

/// Maps a FLAC PICTURE type to the .mpack artwork role: 3 -> "front",
/// 4 -> "back", 7 -> "booklet-page", 8 -> "medium", anything else -> "other".
MUSICPACK_API const char *musicpack_meta_picture_role(int type);

/* ---- MusicBrainz response enrichment (Phase 3C) ---------------------- */

/// Enriches empty manifest fields from a MusicBrainz release document (a
/// release lookup response, or a search envelope containing "releases").
/// First-wins per field: existing values are never overwritten. Fills album
/// title/artists/releaseType/original date, release date/country/label/
/// catalogue, barcode, MB release/group IDs, per-disc medium format, and
/// per-track MB track/recording IDs + ISRC (matched by disc+track number).
/// Identity (source/confidence) is intentionally not touched; the caller
/// sets it from the evidence.
MUSICPACK_API musicpack_status musicpack_mb_apply_release(const char *mb_json,
                                                          musicpack_manifest *m);

/// Returns a confidence string for how strongly \p mb_json (a MusicBrainz
/// release document or search envelope) matches the manifest:
///   "exact"     - the release id equals the manifest's musicbrainzReleaseId
///   "confirmed" - barcode matches, or >=1 ISRC matches with a track-count
///                 match
///   "probable"  - some ISRC or title match
///   "none"      - no usable match (or unparseable input)
MUSICPACK_API const char *musicpack_mb_match_confidence(const char *mb_json,
                                                        const musicpack_manifest *m);

#ifdef __cplusplus
}
#endif
#endif /* MUSICPACK_META_H_ */
