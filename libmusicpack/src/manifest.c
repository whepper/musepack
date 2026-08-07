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
/// \file manifest.c
/// `.mpack` v1 manifest model: parse, validate, free and serialize.

#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include <musicpack/path.h>
#include <musicpack/loudness.h>

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static int
get_req_string(cJSON *obj, const char *key, char **field, musicpack_status *status)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(v) || v->valuestring == 0 || *v->valuestring == '\0') {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    *field = strdup(v->valuestring);
    if (*field == 0) {
        *status = MUSICPACK_ERR_NOMEM;
        return 0;
    }
    return 1;
}

static int
get_opt_string(cJSON *obj, const char *key, char **field, musicpack_status *status)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v == 0)
        return 1;
    if (!cJSON_IsString(v)) {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    *field = strdup(v->valuestring);
    if (*field == 0) {
        *status = MUSICPACK_ERR_NOMEM;
        return 0;
    }
    return 1;
}

static int
get_req_int(cJSON *obj, const char *key, int *out, musicpack_status *status)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < 1 ||
        v->valuedouble != (double) (int) v->valuedouble) {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    *out = (int) v->valuedouble;
    return 1;
}

static int
get_opt_double(cJSON *obj, const char *key, int *present, double *out,
               int (*validate)(double), musicpack_status *status)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v == 0) {
        *present = 0;
        return 1;
    }
    if (!cJSON_IsNumber(v)) {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    if (validate != 0 && validate(v->valuedouble) != MUSICPACK_OK) {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    *present = 1;
    *out = v->valuedouble;
    return 1;
}

static int
is_lower_hex(const char *s)
{
    size_t i;
    if (s == 0)
        return 0;
    for (i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return 0;
    }
    return i == 64;
}

static int
parse_asset(cJSON *o, musicpack_asset *a, musicpack_status *status)
{
    *status = MUSICPACK_OK;
    if (!cJSON_IsObject(o))
        return 0;
    if (!get_req_string(o, "path", &a->path, status))
        return 0;
    if (musicpack_path_validate(a->path) != MUSICPACK_OK) {
        *status = MUSICPACK_ERR_PATH;
        return 0;
    }
    if (!get_opt_string(o, "sha256", &a->sha256, status))
        return 0;
    if (a->sha256 != 0 && !is_lower_hex(a->sha256)) {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    return 1;
}

static int
parse_artists(cJSON *arr, musicpack_artist **out, size_t *count, musicpack_status *status)
{
    cJSON *item;
    int i = 0;

    *out = 0;
    *count = 0;
    if (!cJSON_IsArray(arr))
        return 0;
    if (cJSON_GetArraySize(arr) == 0)
        return 0;
    *out = (musicpack_artist *) calloc((size_t) cJSON_GetArraySize(arr), sizeof **out);
    if (*out == 0) {
        *status = MUSICPACK_ERR_NOMEM;
        return 0;
    }
    cJSON_ArrayForEach(item, arr) {
        musicpack_artist *a = &(*out)[i];
        *status = MUSICPACK_OK;
        if (!get_req_string(item, "name", &a->name, status))
            return 0;
        if (!get_opt_string(item, "role", &a->role, status))
            return 0;
        i++;
    }
    *count = (size_t) i;
    return 1;
}

static int
parse_loudness(cJSON *o, musicpack_loudness *l, musicpack_status *status)
{
    int have_lufs = 0, have_peak = 0;

    l->present = 0;
    if (o == 0)
        return 1;
    if (!cJSON_IsObject(o))
        return 0;
    if (!get_opt_double(o, "trackLUFS", &have_lufs, &l->lufs,
                        musicpack_loudness_validate_lufs, status))
        return 0;
    if (!get_opt_double(o, "truePeakDbTP", &have_peak, &l->true_peak_db,
                        musicpack_loudness_validate_true_peak, status))
        return 0;
    if (!have_lufs || !have_peak)
        return 0; /* both measured values required when the object is present */
    l->present = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* parse                                                               */
/* ------------------------------------------------------------------ */

static int
parse_track(cJSON *o, musicpack_track *t, musicpack_status *status)
{
    cJSON *v;

    if (!cJSON_IsObject(o))
        return 0;
    if (!get_req_int(o, "track", &t->number, status))
        return 0;
    if (!get_req_string(o, "title", &t->title, status))
        return 0;

    v = cJSON_GetObjectItemCaseSensitive(o, "artists");
    if (v != 0 && !parse_artists(v, &t->artists, &t->artist_count, status))
        return 0;

    v = cJSON_GetObjectItemCaseSensitive(o, "identifiers");
    if (v != 0) {
        if (!get_opt_string(v, "isrc", &t->isrc, status))
            return 0;
    }
    v = cJSON_GetObjectItemCaseSensitive(o, "source");
    if (v != 0) {
        if (!get_opt_string(v, "store", &t->source_store, status))
            return 0;
        if (!get_opt_string(v, "trackId", &t->source_track_id, status))
            return 0;
    }
    v = cJSON_GetObjectItemCaseSensitive(o, "sourceAudio");
    if (v != 0) {
        if (!get_opt_string(v, "codec", &t->source_audio_codec, status))
            return 0;
        if (!get_opt_string(v, "md5", &t->source_audio_md5, status))
            return 0;
    }

    if (!get_opt_double(o, "duration", &t->has_duration, &t->duration, 0, status))
        return 0;
    if (t->has_duration && t->duration <= 0.0) {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }

    v = cJSON_GetObjectItemCaseSensitive(o, "loudness");
    if (v != 0 && !parse_loudness(v, &t->loudness, status))
        return 0;

    v = cJSON_GetObjectItemCaseSensitive(o, "audio");
    if (v == 0 || !parse_asset(v, &t->audio, status))
        return 0;
    return 1;
}

static int
parse_disc(cJSON *o, musicpack_disc *d, musicpack_status *status)
{
    cJSON *tracks;
    cJSON *item;
    int i = 0;

    if (!cJSON_IsObject(o))
        return 0;
    if (!get_req_int(o, "disc", &d->disc, status))
        return 0;
    if (!get_opt_string(o, "title", &d->title, status))
        return 0;
    tracks = cJSON_GetObjectItemCaseSensitive(o, "tracks");
    if (!cJSON_IsArray(tracks) || cJSON_GetArraySize(tracks) == 0) {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    d->tracks = (musicpack_track *) calloc((size_t) cJSON_GetArraySize(tracks), sizeof *d->tracks);
    if (d->tracks == 0) {
        *status = MUSICPACK_ERR_NOMEM;
        return 0;
    }
    cJSON_ArrayForEach(item, tracks) {
        *status = MUSICPACK_OK;
        if (!parse_track(item, &d->tracks[i], status))
            return 0;
        i++;
    }
    d->track_count = (size_t) i;
    return 1;
}

static int
check_dup_paths(const musicpack_manifest *m, musicpack_status *status)
{
    /* Collect every referenced asset path and reject duplicates. */
    const char *paths[4096];
    size_t count = 0;
    size_t d, t, a;

    if (m->disc_count * 4 + m->artwork_count + m->booklet_count +
        m->lyrics_count + m->extras_count > 4096) {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    for (d = 0; d < m->disc_count; d++)
        for (t = 0; t < m->discs[d].track_count; t++)
            paths[count++] = m->discs[d].tracks[t].audio.path;
    for (a = 0; a < m->artwork_count; a++) paths[count++] = m->artwork[a].asset.path;
    for (a = 0; a < m->booklet_count; a++) paths[count++] = m->booklet[a].path;
    for (a = 0; a < m->lyrics_count; a++) paths[count++] = m->lyrics[a].path;
    for (a = 0; a < m->extras_count; a++) paths[count++] = m->extras[a].path;

    for (a = 0; a < count; a++) {
        size_t b;
        for (b = a + 1; b < count; b++) {
            if (strcmp(paths[a], paths[b]) == 0) {
                *status = MUSICPACK_ERR_INVALID;
                return 0;
            }
        }
    }
    return 1;
}

musicpack_status
musicpack_manifest_parse_tree(const cJSON *root, musicpack_manifest *m)
{
    musicpack_status status = MUSICPACK_OK;
    cJSON *v, *item;
    int i, n;
    size_t d;

    /* format + version */
    v = cJSON_GetObjectItemCaseSensitive(root, "format");
    if (!cJSON_IsString(v) || strcmp(v->valuestring, MUSICPACK_FORMAT) != 0)
        return MUSICPACK_ERR_INVALID;
    v = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(v) || (int) v->valuedouble != MUSICPACK_VERSION_SCHEMA ||
        v->valuedouble != (double) (int) v->valuedouble)
        return MUSICPACK_ERR_VERSION;

    /* album */
    v = cJSON_GetObjectItemCaseSensitive(root, "album");
    if (!cJSON_IsObject(v))
        return MUSICPACK_ERR_INVALID;
    if (!get_req_string(v, "title", &m->album_title, &status))
        return status;
    {
        cJSON *artists = cJSON_GetObjectItemCaseSensitive(v, "artists");
        if (!parse_artists(artists, &m->album_artists, &m->album_artist_count, &status))
            return status;
    }
    if (!get_opt_string(v, "releaseDate", &m->release_date, &status))
        return status;
    {
        cJSON *genres = cJSON_GetObjectItemCaseSensitive(v, "genres");
        if (genres != 0) {
            if (!cJSON_IsArray(genres))
                return MUSICPACK_ERR_INVALID;
            n = cJSON_GetArraySize(genres);
            if (n > 0) {
                m->genres = (char **) calloc((size_t) n, sizeof *m->genres);
                if (m->genres == 0)
                    return MUSICPACK_ERR_NOMEM;
                i = 0;
                cJSON_ArrayForEach(item, genres) {
                    if (!cJSON_IsString(item))
                        return MUSICPACK_ERR_INVALID;
                    m->genres[i] = strdup(item->valuestring);
                    if (m->genres[i] == 0)
                        return MUSICPACK_ERR_NOMEM;
                    i++;
                }
                m->genre_count = (size_t) i;
            }
        }
    }

    /* identifiers + identity */
    v = cJSON_GetObjectItemCaseSensitive(root, "identifiers");
    if (v != 0) {
        if (!cJSON_IsObject(v))
            return MUSICPACK_ERR_INVALID;
        if (!get_opt_string(v, "musicbrainzReleaseId", &m->musicbrainz_release_id, &status))
            return status;
        if (!get_opt_string(v, "barcode", &m->barcode, &status))
            return status;
    }
    v = cJSON_GetObjectItemCaseSensitive(root, "identity");
    if (v != 0) {
        if (!cJSON_IsObject(v))
            return MUSICPACK_ERR_INVALID;
        if (!get_opt_string(v, "source", &m->identity_source, &status))
            return status;
        if (!get_opt_string(v, "confidence", &m->identity_confidence, &status))
            return status;
    }

    /* source */
    v = cJSON_GetObjectItemCaseSensitive(root, "source");
    if (v != 0) {
        if (!cJSON_IsObject(v))
            return MUSICPACK_ERR_INVALID;
        if (!get_opt_string(v, "type", &m->source_type, &status))
            return status;
        if (!get_opt_string(v, "store", &m->source_store, &status))
            return status;
        if (!get_opt_string(v, "sourceId", &m->source_id, &status))
            return status;
    }

    /* media */
    v = cJSON_GetObjectItemCaseSensitive(root, "media");
    if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) == 0)
        return MUSICPACK_ERR_INVALID;
    n = cJSON_GetArraySize(v);
    m->discs = (musicpack_disc *) calloc((size_t) n, sizeof *m->discs);
    if (m->discs == 0)
        return MUSICPACK_ERR_NOMEM;
    i = 0;
    cJSON_ArrayForEach(item, v) {
        if (!parse_disc(item, &m->discs[i], &status))
            return status;
        i++;
    }
    m->disc_count = (size_t) i;

    /* unique disc / track numbering */
    for (d = 0; d < m->disc_count; d++) {
        size_t t, u;
        for (t = 0; t < m->discs[d].track_count; t++) {
            for (u = t + 1; u < m->discs[d].track_count; u++)
                if (m->discs[d].tracks[t].number == m->discs[d].tracks[u].number)
                    return MUSICPACK_ERR_INVALID;
        }
        for (u = d + 1; u < m->disc_count; u++)
            if (m->discs[d].disc == m->discs[u].disc)
                return MUSICPACK_ERR_INVALID;
    }

    /* artwork / booklet / lyrics / extras */
    v = cJSON_GetObjectItemCaseSensitive(root, "artwork");
    if (v != 0) {
        if (!cJSON_IsArray(v))
            return MUSICPACK_ERR_INVALID;
        n = cJSON_GetArraySize(v);
        m->artwork = (musicpack_artwork *) calloc((size_t) n, sizeof *m->artwork);
        if (m->artwork == 0)
            return MUSICPACK_ERR_NOMEM;
        i = 0;
        cJSON_ArrayForEach(item, v) {
            if (!cJSON_IsObject(item))
                return MUSICPACK_ERR_INVALID;
            if (!get_req_string(item, "role", &m->artwork[i].role, &status))
                return status;
            if (!parse_asset(item, &m->artwork[i].asset, &status))
                return status;
            i++;
        }
        m->artwork_count = (size_t) i;
    }
#define PARSE_ASSET_ARRAY(key, field, countfield)                              \
    v = cJSON_GetObjectItemCaseSensitive(root, key);                           \
    if (v != 0) {                                                              \
        if (!cJSON_IsArray(v))                                                 \
            return MUSICPACK_ERR_INVALID;                                      \
        n = cJSON_GetArraySize(v);                                             \
        m->field = (musicpack_asset *) calloc((size_t) n, sizeof *m->field);   \
        if (m->field == 0)                                                     \
            return MUSICPACK_ERR_NOMEM;                                        \
        i = 0;                                                                 \
        cJSON_ArrayForEach(item, v) {                                          \
            if (!parse_asset(item, &m->field[i], &status))                     \
                return status;                                                 \
            i++;                                                               \
        }                                                                      \
        m->countfield = (size_t) i;                                            \
    }
    PARSE_ASSET_ARRAY("booklet", booklet, booklet_count);
    PARSE_ASSET_ARRAY("lyrics", lyrics, lyrics_count);
    PARSE_ASSET_ARRAY("extras", extras, extras_count);
#undef PARSE_ASSET_ARRAY

    /* album loudness */
    v = cJSON_GetObjectItemCaseSensitive(root, "loudness");
    if (v != 0) {
        int have_lufs = 0, have_peak = 0;
        if (!cJSON_IsObject(v))
            return MUSICPACK_ERR_INVALID;
        if (!get_opt_double(v, "albumLUFS", &have_lufs, &m->album_loudness.lufs,
                            musicpack_loudness_validate_lufs, &status))
            return status;
        if (!get_opt_double(v, "albumTruePeakDbTP", &have_peak,
                            &m->album_loudness.true_peak_db,
                            musicpack_loudness_validate_true_peak, &status))
            return status;
        if (have_lufs || have_peak)
            m->has_album_loudness = 1;
    }

    /* provenance */
    v = cJSON_GetObjectItemCaseSensitive(root, "provenance");
    if (v != 0) {
        if (!cJSON_IsObject(v))
            return MUSICPACK_ERR_INVALID;
        if (!get_opt_string(v, "tool", &m->provenance_tool, &status))
            return status;
        if (!get_opt_string(v, "toolVersion", &m->provenance_tool_version, &status))
            return status;
    }

    if (!check_dup_paths(m, &status))
        return status;

    return MUSICPACK_OK;
}

musicpack_manifest *
musicpack_manifest_parse(const char *json, musicpack_status *status)
{
    musicpack_manifest *m;
    cJSON *root;
    musicpack_status local = MUSICPACK_OK;
    size_t len;

    if (status == 0)
        status = &local;
    *status = MUSICPACK_OK;
    if (json == 0) {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    len = strlen(json);
    if (len > 16u * 1024u * 1024u) {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    root = cJSON_ParseWithLength(json, len);
    if (root == 0) {
        *status = MUSICPACK_ERR_JSON;
        return 0;
    }

    m = (musicpack_manifest *) calloc(1, sizeof *m);
    if (m == 0) {
        cJSON_Delete(root);
        *status = MUSICPACK_ERR_NOMEM;
        return 0;
    }
    *status = musicpack_manifest_parse_tree(root, m);
    cJSON_Delete(root);
    if (*status != MUSICPACK_OK) {
        musicpack_manifest_free(m);
        return 0;
    }
    return m;
}

void
musicpack_manifest_clear(musicpack_manifest *m)
{
    size_t i;

    if (m == 0)
        return;
    free(m->album_title);
    for (i = 0; i < m->album_artist_count; i++) {
        free(m->album_artists[i].name);
        free(m->album_artists[i].role);
    }
    free(m->album_artists);
    free(m->release_date);
    for (i = 0; i < m->genre_count; i++)
        free(m->genres[i]);
    free(m->genres);
    free(m->musicbrainz_release_id);
    free(m->barcode);
    free(m->identity_source);
    free(m->identity_confidence);
    free(m->source_type);
    free(m->source_store);
    free(m->source_id);

    for (i = 0; i < m->disc_count; i++) {
        musicpack_disc *d = &m->discs[i];
        size_t t;
        free(d->title);
        for (t = 0; t < d->track_count; t++) {
            musicpack_track *tr = &d->tracks[t];
            size_t a;
            free(tr->title);
            for (a = 0; a < tr->artist_count; a++) {
                free(tr->artists[a].name);
                free(tr->artists[a].role);
            }
            free(tr->artists);
            free(tr->isrc);
            free(tr->source_store);
            free(tr->source_track_id);
            free(tr->source_audio_codec);
            free(tr->source_audio_md5);
            free(tr->audio.path);
            free(tr->audio.sha256);
        }
        free(d->tracks);
    }
    free(m->discs);

    for (i = 0; i < m->artwork_count; i++) {
        free(m->artwork[i].role);
        free(m->artwork[i].asset.path);
        free(m->artwork[i].asset.sha256);
    }
    free(m->artwork);
#define FREE_ASSETS(field, count)                                              \
    for (i = 0; i < m->count; i++) { free(m->field[i].path); free(m->field[i].sha256); } \
    free(m->field);
    FREE_ASSETS(booklet, booklet_count);
    FREE_ASSETS(lyrics, lyrics_count);
    FREE_ASSETS(extras, extras_count);
#undef FREE_ASSETS

    free(m->provenance_tool);
    free(m->provenance_tool_version);
}

void
musicpack_manifest_free(musicpack_manifest *m)
{
    if (m == 0)
        return;
    musicpack_manifest_clear(m);
    free(m);
}

/* ------------------------------------------------------------------ */
/* serialize                                                           */
/* ------------------------------------------------------------------ */

static cJSON *
artists_to_json(const musicpack_artist *artists, size_t count)
{
    cJSON *arr = cJSON_CreateArray();
    size_t i;
    for (i = 0; i < count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", artists[i].name);
        if (artists[i].role != 0)
            cJSON_AddStringToObject(o, "role", artists[i].role);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

static cJSON *
asset_to_json(const musicpack_asset *a)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "path", a->path);
    if (a->sha256 != 0)
        cJSON_AddStringToObject(o, "sha256", a->sha256);
    return o;
}

static cJSON *
build_tree(const musicpack_manifest *m)
{
    cJSON *root, *o, *arr, *item;
    size_t i;

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "format", MUSICPACK_FORMAT);
    cJSON_AddNumberToObject(root, "version", MUSICPACK_VERSION_SCHEMA);

    o = cJSON_AddObjectToObject(root, "album");
    cJSON_AddStringToObject(o, "title", m->album_title);
    cJSON_AddItemToObject(o, "artists", artists_to_json(m->album_artists, m->album_artist_count));
    if (m->release_date != 0)
        cJSON_AddStringToObject(o, "releaseDate", m->release_date);
    if (m->genre_count > 0) {
        cJSON *g = cJSON_AddArrayToObject(o, "genres");
        for (i = 0; i < m->genre_count; i++)
            cJSON_AddItemToArray(g, cJSON_CreateString(m->genres[i]));
    }

    if (m->musicbrainz_release_id != 0 || m->barcode != 0) {
        o = cJSON_AddObjectToObject(root, "identifiers");
        if (m->musicbrainz_release_id != 0)
            cJSON_AddStringToObject(o, "musicbrainzReleaseId", m->musicbrainz_release_id);
        if (m->barcode != 0)
            cJSON_AddStringToObject(o, "barcode", m->barcode);
    }
    if (m->identity_source != 0 || m->identity_confidence != 0) {
        o = cJSON_AddObjectToObject(root, "identity");
        if (m->identity_source != 0)
            cJSON_AddStringToObject(o, "source", m->identity_source);
        if (m->identity_confidence != 0)
            cJSON_AddStringToObject(o, "confidence", m->identity_confidence);
    }
    if (m->source_type != 0 || m->source_store != 0 || m->source_id != 0) {
        o = cJSON_AddObjectToObject(root, "source");
        if (m->source_type != 0)
            cJSON_AddStringToObject(o, "type", m->source_type);
        if (m->source_store != 0)
            cJSON_AddStringToObject(o, "store", m->source_store);
        if (m->source_id != 0)
            cJSON_AddStringToObject(o, "sourceId", m->source_id);
    }

    arr = cJSON_AddArrayToObject(root, "media");
    for (i = 0; i < m->disc_count; i++) {
        const musicpack_disc *d = &m->discs[i];
        size_t t;
        o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "disc", d->disc);
        if (d->title != 0)
            cJSON_AddStringToObject(o, "title", d->title);
        item = cJSON_AddArrayToObject(o, "tracks");
        for (t = 0; t < d->track_count; t++) {
            const musicpack_track *tr = &d->tracks[t];
            cJSON *to = cJSON_CreateObject();
            cJSON_AddNumberToObject(to, "track", tr->number);
            cJSON_AddStringToObject(to, "title", tr->title);
            if (tr->artist_count > 0)
                cJSON_AddItemToObject(to, "artists",
                                      artists_to_json(tr->artists, tr->artist_count));
            if (tr->isrc != 0) {
                cJSON *id = cJSON_AddObjectToObject(to, "identifiers");
                cJSON_AddStringToObject(id, "isrc", tr->isrc);
            }
            if (tr->source_store != 0 || tr->source_track_id != 0) {
                cJSON *src = cJSON_AddObjectToObject(to, "source");
                if (tr->source_store != 0)
                    cJSON_AddStringToObject(src, "store", tr->source_store);
                if (tr->source_track_id != 0)
                    cJSON_AddStringToObject(src, "trackId", tr->source_track_id);
            }
            if (tr->source_audio_codec != 0 || tr->source_audio_md5 != 0) {
                cJSON *sa = cJSON_AddObjectToObject(to, "sourceAudio");
                if (tr->source_audio_codec != 0)
                    cJSON_AddStringToObject(sa, "codec", tr->source_audio_codec);
                if (tr->source_audio_md5 != 0)
                    cJSON_AddStringToObject(sa, "md5", tr->source_audio_md5);
            }
            if (tr->has_duration)
                cJSON_AddNumberToObject(to, "duration", tr->duration);
            if (tr->loudness.present) {
                cJSON *lo = cJSON_AddObjectToObject(to, "loudness");
                cJSON_AddNumberToObject(lo, "trackLUFS", tr->loudness.lufs);
                cJSON_AddNumberToObject(lo, "truePeakDbTP", tr->loudness.true_peak_db);
            }
            cJSON_AddItemToObject(to, "audio", asset_to_json(&tr->audio));
            cJSON_AddItemToArray(item, to);
        }
        cJSON_AddItemToArray(arr, o);
    }

    if (m->artwork_count > 0) {
        arr = cJSON_AddArrayToObject(root, "artwork");
        for (i = 0; i < m->artwork_count; i++) {
            cJSON *w = cJSON_CreateObject();
            cJSON_AddStringToObject(w, "role", m->artwork[i].role);
            cJSON_AddItemToObject(w, "path", cJSON_CreateString(m->artwork[i].asset.path));
            if (m->artwork[i].asset.sha256 != 0)
                cJSON_AddStringToObject(w, "sha256", m->artwork[i].asset.sha256);
            cJSON_AddItemToArray(arr, w);
        }
    }
#define ADD_ASSET_ARRAY(key, field, count)                                     \
    if (m->count > 0) {                                                        \
        arr = cJSON_AddArrayToObject(root, key);                               \
        for (i = 0; i < m->count; i++)                                         \
            cJSON_AddItemToArray(arr, asset_to_json(&m->field[i]));            \
    }
    ADD_ASSET_ARRAY("booklet", booklet, booklet_count);
    ADD_ASSET_ARRAY("lyrics", lyrics, lyrics_count);
    ADD_ASSET_ARRAY("extras", extras, extras_count);
#undef ADD_ASSET_ARRAY

    if (m->has_album_loudness) {
        o = cJSON_AddObjectToObject(root, "loudness");
        cJSON_AddNumberToObject(o, "albumLUFS", m->album_loudness.lufs);
        cJSON_AddNumberToObject(o, "albumTruePeakDbTP", m->album_loudness.true_peak_db);
    }
    if (m->provenance_tool != 0 || m->provenance_tool_version != 0) {
        o = cJSON_AddObjectToObject(root, "provenance");
        if (m->provenance_tool != 0)
            cJSON_AddStringToObject(o, "tool", m->provenance_tool);
        if (m->provenance_tool_version != 0)
            cJSON_AddStringToObject(o, "toolVersion", m->provenance_tool_version);
    }

    return root;
}

/* ------------------------------------------------------------------ */
/* canonical JSON printer (2-space indent, compact numbers)           */
/* ------------------------------------------------------------------ */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} json_out;

static int
json_put(json_out *o, const char *s, size_t n)
{
    if (o->len + n + 1 > o->cap) {
        size_t newcap = o->cap == 0 ? 256 : o->cap * 2;
        while (newcap < o->len + n + 1)
            newcap *= 2;
        {
            char *nb = (char *) realloc(o->buf, newcap);
            if (nb == 0)
                return -1;
            o->buf = nb;
            o->cap = newcap;
        }
    }
    memcpy(o->buf + o->len, s, n);
    o->len += n;
    o->buf[o->len] = '\0';
    return 0;
}

static int
json_indent(json_out *o, int depth)
{
    int i;
    for (i = 0; i < depth; i++)
        if (json_put(o, "  ", 2) != 0)
            return -1;
    return 0;
}

static void
json_number(json_out *o, double v)
{
    char tmp[64];
    if (v == (double) (long long) v && v >= -9.2e18 && v <= 9.2e18) {
        snprintf(tmp, sizeof tmp, "%lld", (long long) v);
    } else {
        snprintf(tmp, sizeof tmp, "%.8g", v);
    }
    json_put(o, tmp, strlen(tmp));
}

static void
json_string(json_out *o, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    json_put(o, "\"", 1);
    if (s != 0) {
        const unsigned char *p = (const unsigned char *) s;
        while (*p != '\0') {
            unsigned char c = *p;
            if (c == '"' || c == '\\') {
                char esc[2] = { '\\', (char) c };
                json_put(o, esc, 2);
            } else if (c < 0x20) {
                char esc[6];
                esc[0] = '\\'; esc[1] = 'u'; esc[2] = '0'; esc[3] = '0';
                esc[4] = hex[c >> 4]; esc[5] = hex[c & 0xF];
                json_put(o, esc, 6);
            } else {
                json_put(o, (const char *) p, 1);
            }
            p++;
        }
    }
    json_put(o, "\"", 1);
}

static void
json_print_value(json_out *o, const cJSON *item, int depth)
{
    const cJSON *child;
    switch (item->type & 0xFF) {
    case cJSON_NULL: json_put(o, "null", 4); break;
    case cJSON_False: json_put(o, "false", 5); break;
    case cJSON_True: json_put(o, "true", 4); break;
    case cJSON_Number: json_number(o, item->valuedouble); break;
    case cJSON_String: json_string(o, item->valuestring); break;
    case cJSON_Array:
        if (cJSON_GetArraySize(item) == 0) {
            json_put(o, "[]", 2);
            break;
        }
        json_put(o, "[\n", 2);
        cJSON_ArrayForEach(child, item) {
            json_indent(o, depth + 1);
            json_print_value(o, child, depth + 1);
            json_put(o, child->next != 0 ? ",\n" : "\n", child->next != 0 ? 2 : 1);
        }
        json_indent(o, depth);
        json_put(o, "]", 1);
        break;
    case cJSON_Object:
        if (cJSON_GetArraySize(item) == 0) {
            json_put(o, "{}", 2);
            break;
        }
        json_put(o, "{\n", 2);
        cJSON_ArrayForEach(child, item) {
            json_indent(o, depth + 1);
            json_string(o, child->string);
            json_put(o, ": ", 2);
            json_print_value(o, child, depth + 1);
            json_put(o, child->next != 0 ? ",\n" : "\n", child->next != 0 ? 2 : 1);
        }
        json_indent(o, depth);
        json_put(o, "}", 1);
        break;
    default:
        json_put(o, "null", 4);
        break;
    }
}

static char *
musicpack_json_print(const cJSON *root)
{
    json_out o = { 0, 0, 0 };
    if (root == 0)
        return 0;
    json_print_value(&o, root, 0);
    json_put(&o, "\n", 1);
    return o.buf;
}

static void
merge_into(cJSON *dst, const cJSON *src)
{
    const cJSON *child;

    if (dst == 0 || src == 0 || !cJSON_IsObject(dst))
        return;
    cJSON_ArrayForEach(child, src) {
        cJSON *existing = cJSON_GetObjectItemCaseSensitive(dst, child->string);
        if (existing != 0 && cJSON_IsObject(existing) && cJSON_IsObject(child)) {
            merge_into(existing, child);
        } else if (existing != 0 && cJSON_IsArray(existing) && cJSON_IsArray(child)) {
            int idx = 0;
            const cJSON *el;
            cJSON_ArrayForEach(el, child) {
                cJSON *dst_el = cJSON_GetArrayItem(existing, idx);
                if (dst_el != 0 && cJSON_IsObject(dst_el) && cJSON_IsObject(el))
                    merge_into(dst_el, el);
                idx++;
            }
        } else {
            cJSON *copy = cJSON_Duplicate(child, 1);
            if (copy != 0)
                cJSON_ReplaceItemInObjectCaseSensitive(dst, child->string, copy);
        }
    }
}

musicpack_status
musicpack_manifest_write_with_original(const musicpack_manifest *m,
                                       const cJSON *original, char **json_out)
{
    cJSON *tree;

    if (m == 0 || json_out == 0)
        return MUSICPACK_ERR_INVALID;
    tree = build_tree(m);
    if (tree == 0)
        return MUSICPACK_ERR_NOMEM;

    if (original != 0) {
        /* Preserve unknown fields: merge the typed values into a copy of the
           original document so extension keys survive the round-trip. */
        cJSON *copy = cJSON_Duplicate(original, 1);
        if (copy == 0) {
            cJSON_Delete(tree);
            return MUSICPACK_ERR_NOMEM;
        }
        merge_into(copy, tree);
        cJSON_Delete(tree);
        tree = copy;
    }

    *json_out = musicpack_json_print(tree);
    cJSON_Delete(tree);
    return *json_out != 0 ? MUSICPACK_OK : MUSICPACK_ERR_NOMEM;
}

musicpack_status
musicpack_manifest_write(const musicpack_manifest *m, char **json_out)
{
    return musicpack_manifest_write_with_original(m, 0, json_out);
}
