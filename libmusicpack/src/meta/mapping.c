/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD-2-Clause; see include/musicpack/meta.h for the full text.)
*/
/// \file mapping.c
/// Tag-set -> canonical manifest mapping and manifest -> APEv2 projection.
///
/// Two source vocabularies are accepted (Vorbis Comment keys, upper-cased,
/// and APEv2 keys, title-cased) via case-insensitive alias tables. Mapping is
/// first-wins per field: the first file's album metadata wins, and explicit
/// CLI overrides are applied by the caller (they pre-seed the manifest).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <musicpack/meta.h>

/* ---- small helpers -------------------------------------------------- */

static int
ci_eq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char) *a;
        unsigned char cb = (unsigned char) *b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char) (ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char) (cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static const char *
field_value(const musicpack_tag_set *tags, const char *const *names, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        const musicpack_tag *t = musicpack_tag_set_get(tags, names[i]);
        if (t != 0 && !t->is_binary)
            return t->value;
    }
    return 0;
}

/* First alias with any values wins; writes up to cap pointers into out. */
static size_t
field_values(const musicpack_tag_set *tags, const char *const *names, size_t n,
             const musicpack_tag **out, size_t cap)
{
    size_t i;
    for (i = 0; i < n; i++) {
        size_t k = musicpack_tag_set_get_all(tags, names[i], out, cap);
        if (k > 0)
            return k;
    }
    return 0;
}

static musicpack_status
set_dup(char **dst, const char *v)
{
    if (*dst != 0 || v == 0 || *v == '\0')
        return MUSICPACK_OK;
    *dst = strdup(v);
    return *dst != 0 ? MUSICPACK_OK : MUSICPACK_ERR_NOMEM;
}

static musicpack_status
append_artists(musicpack_artist **arr, size_t *count, const musicpack_tag **vals,
               size_t n, const char *role)
{
    musicpack_artist *na;
    size_t base = *count;
    size_t i;

    if (n == 0)
        return MUSICPACK_OK;
    na = (musicpack_artist *) realloc(*arr, (base + n) * sizeof *na);
    if (na == 0)
        return MUSICPACK_ERR_NOMEM;
    *arr = na;
    for (i = 0; i < n; i++) {
        na[base + i].name = strdup(vals[i]->value);
        na[base + i].role = strdup(role);
        if (na[base + i].name == 0 || na[base + i].role == 0) {
            if (na[base + i].name != 0) free(na[base + i].name);
            if (na[base + i].role != 0) free(na[base + i].role);
            return MUSICPACK_ERR_NOMEM;
        }
    }
    *count = base + n;
    return MUSICPACK_OK;
}

/* ---- alias tables --------------------------------------------------- */

static const char *const A_ALBUM[]        = { "ALBUM" };
static const char *const A_ALBUM_ARTIST[] = { "ALBUMARTIST", "ALBUM ARTIST" };
static const char *const A_TITLE[]        = { "TITLE" };
static const char *const A_ARTIST[]       = { "ARTIST" };
static const char *const A_TRACKNUM[]     = { "TRACKNUMBER", "Track" };
static const char *const A_DATE[]         = { "DATE", "YEAR" };
static const char *const A_ORIGDATE[]     = { "ORIGINALDATE", "ORIGINAL YEAR" };
static const char *const A_GENRE[]        = { "GENRE" };
static const char *const A_LABEL[]        = { "PUBLISHER", "LABEL", "ORGANIZATION" };
static const char *const A_CATALOGUE[]    = { "CATALOGNUMBER", "CATALOGUENUMBER",
                                               "CATALOG #" };
static const char *const A_BARCODE[]      = { "BARCODE" };
static const char *const A_ISRC[]         = { "ISRC" };
static const char *const A_MB_RELID[]     = { "MUSICBRAINZ_ALBUMID",
                                               "MUSICBRAINZ_RELEASEID",
                                               "MusicBrainz Album Id" };
static const char *const A_MB_RGID[]      = { "MUSICBRAINZ_RELEASEGROUPID",
                                               "RELEASEGROUPID",
                                               "MusicBrainz Release Group Id" };
static const char *const A_MB_RECID[]     = { "MUSICBRAINZ_RECORDINGID",
                                               "MusicBrainz Recording Id" };
static const char *const A_MB_RECID_LEGACY[] = { "MUSICBRAINZ_TRACKID",
                                                  "MusicBrainz Track Id" };
static const char *const A_MB_TRKID[]     = { "MUSICBRAINZ_RELEASETRACKID",
                                               "MusicBrainz Release Track Id" };
static const char *const A_MB_TYPE[]      = { "MUSICBRAINZ_ALBUMTYPE",
                                               "RELEASETYPE",
                                               "MusicBrainz Album Type" };
static const char *const A_MB_COUNTRY[]   = { "MUSICBRAINZ_ALBUMCOUNTRY",
                                               "RELEASECOUNTRY",
                                               "MusicBrainz Album Country" };
static const char *const A_SOURCE[]       = { "SOURCE" };
static const char *const A_SOURCEID[]     = { "SOURCEID" };
static const char *const A_COMPOSER[]     = { "COMPOSER" };
static const char *const A_PERFORMER[]    = { "PERFORMER" };
static const char *const A_CONDUCTOR[]    = { "CONDUCTOR" };
static const char *const A_REMIXER[]      = { "REMIXER" };
static const char *const A_AUTHOR[]       = { "AUTHOR" };

/* ---- public helpers ------------------------------------------------- */

int
musicpack_meta_parse_track_number(const char *value, int *out)
{
    int n = 0, digits = 0;

    if (value == 0 || out == 0)
        return 0;
    while (*value >= '0' && *value <= '9') {
        n = n * 10 + (*value - '0');
        digits++;
        value++;
    }
    if (digits == 0 || n < 1)
        return 0;
    *out = n;
    return 1;
}

const char *
musicpack_meta_release_type_from_tag(const char *value)
{
    if (value == 0 || *value == '\0')
        return 0;
    if (ci_eq(value, "album"))         return "album";
    if (ci_eq(value, "single"))        return "single";
    if (ci_eq(value, "ep"))            return "ep";
    if (ci_eq(value, "compilation"))   return "compilation";
    if (ci_eq(value, "soundtrack"))    return "soundtrack";
    if (ci_eq(value, "live") ||
        ci_eq(value, "live album"))    return "live-album";
    if (ci_eq(value, "remix") ||
        ci_eq(value, "remix album"))   return "remix-album";
    return "other";
}

/* Is the SOURCE tag value a store/service name rather than a rip type? */
static int
source_is_store(const char *v)
{
    if (ci_eq(v, "cd-rip") || ci_eq(v, "cdrip") || ci_eq(v, "cd rip") ||
        ci_eq(v, "cd"))
        return 0;
    if (ci_eq(v, "digital-download") || ci_eq(v, "download") ||
        ci_eq(v, "digital"))
        return 0;
    return 1;
}

/* ---- album / release / identifier mapping --------------------------- */

musicpack_status
musicpack_tag_map_album(const musicpack_tag_set *tags, musicpack_manifest *m)
{
    const musicpack_tag *vals[64];
    musicpack_status st;
    size_t n, i;
    const char *v;

    if (tags == 0 || m == 0)
        return MUSICPACK_ERR_INVALID;

    st = set_dup(&m->album_title, field_value(tags, A_ALBUM, sizeof A_ALBUM / sizeof *A_ALBUM));
    if (st != MUSICPACK_OK)
        return st;
    if (m->album_artist_count == 0) {
        n = field_values(tags, A_ALBUM_ARTIST, sizeof A_ALBUM_ARTIST / sizeof *A_ALBUM_ARTIST, vals, 64);
        if (n > 0) {
            st = append_artists(&m->album_artists, &m->album_artist_count,
                                vals, n, "main");
            if (st != MUSICPACK_OK)
                return st;
        }
    }
    if (m->release_type == 0) {
        v = field_value(tags, A_MB_TYPE, sizeof A_MB_TYPE / sizeof *A_MB_TYPE);
        if (v != 0) {
            m->release_type = strdup(musicpack_meta_release_type_from_tag(v));
            if (m->release_type == 0)
                return MUSICPACK_ERR_NOMEM;
        }
    }
    st = set_dup(&m->original_release_date, field_value(tags, A_ORIGDATE, sizeof A_ORIGDATE / sizeof *A_ORIGDATE));
    if (st != MUSICPACK_OK)
        return st;
    if (m->genre_count == 0) {
        n = field_values(tags, A_GENRE, sizeof A_GENRE / sizeof *A_GENRE, vals, 64);
        if (n > 0) {
            m->genres = (char **) calloc(n, sizeof *m->genres);
            if (m->genres == 0)
                return MUSICPACK_ERR_NOMEM;
            for (i = 0; i < n; i++) {
                m->genres[i] = strdup(vals[i]->value);
                if (m->genres[i] == 0)
                    return MUSICPACK_ERR_NOMEM;
            }
            m->genre_count = n;
        }
    }

    /* release */
    st = set_dup(&m->release.release_date, field_value(tags, A_DATE, sizeof A_DATE / sizeof *A_DATE));
    if (st != MUSICPACK_OK)
        return st;
    st = set_dup(&m->release.country, field_value(tags, A_MB_COUNTRY, sizeof A_MB_COUNTRY / sizeof *A_MB_COUNTRY));
    if (st != MUSICPACK_OK)
        return st;
    st = set_dup(&m->release.label, field_value(tags, A_LABEL, sizeof A_LABEL / sizeof *A_LABEL));
    if (st != MUSICPACK_OK)
        return st;
    st = set_dup(&m->release.catalogue_number, field_value(tags, A_CATALOGUE, sizeof A_CATALOGUE / sizeof *A_CATALOGUE));
    if (st != MUSICPACK_OK)
        return st;
    if (m->release.release_date != 0 || m->release.country != 0 ||
        m->release.label != 0 || m->release.catalogue_number != 0 ||
        m->release.edition != 0 || m->release.notes != 0)
        m->release.present = 1;

    /* identifiers */
    st = set_dup(&m->barcode, field_value(tags, A_BARCODE, sizeof A_BARCODE / sizeof *A_BARCODE));
    if (st != MUSICPACK_OK)
        return st;
    st = set_dup(&m->musicbrainz_release_id, field_value(tags, A_MB_RELID, sizeof A_MB_RELID / sizeof *A_MB_RELID));
    if (st != MUSICPACK_OK)
        return st;
    st = set_dup(&m->musicbrainz_release_group_id, field_value(tags, A_MB_RGID, sizeof A_MB_RGID / sizeof *A_MB_RGID));
    if (st != MUSICPACK_OK)
        return st;

    /* source provenance (never release identity) */
    if (m->source_store == 0 && m->source_type == 0) {
        v = field_value(tags, A_SOURCE, sizeof A_SOURCE / sizeof *A_SOURCE);
        if (v != 0 && *v != '\0') {
            if (source_is_store(v)) {
                m->source_store = strdup(v);
                if (m->source_store == 0)
                    return MUSICPACK_ERR_NOMEM;
                m->source_type = strdup("digital-download");
                if (m->source_type == 0)
                    return MUSICPACK_ERR_NOMEM;
            } else {
                m->source_type = strdup("cd-rip");
                if (m->source_type == 0)
                    return MUSICPACK_ERR_NOMEM;
            }
        }
    }
    st = set_dup(&m->source_id, field_value(tags, A_SOURCEID, sizeof A_SOURCEID / sizeof *A_SOURCEID));
    if (st != MUSICPACK_OK)
        return st;

    return MUSICPACK_OK;
}

/* ---- per-track mapping ---------------------------------------------- */

musicpack_status
musicpack_tag_map_track(const musicpack_tag_set *tags, musicpack_track *t)
{
    const musicpack_tag *vals[64];
    musicpack_status st;
    size_t n;
    const char *v;

    if (tags == 0 || t == 0)
        return MUSICPACK_ERR_INVALID;

    st = set_dup(&t->title, field_value(tags, A_TITLE, sizeof A_TITLE / sizeof *A_TITLE));
    if (st != MUSICPACK_OK)
        return st;
    if (t->number == 0) {
        v = field_value(tags, A_TRACKNUM, sizeof A_TRACKNUM / sizeof *A_TRACKNUM);
        if (musicpack_meta_parse_track_number(v, &t->number) == 0)
            t->number = 0;
    }
    if (t->artist_count == 0) {
        n = field_values(tags, A_ARTIST, sizeof A_ARTIST / sizeof *A_ARTIST, vals, 64);
        st = append_artists(&t->artists, &t->artist_count, vals, n, "main");
        if (st != MUSICPACK_OK)
            return st;
        n = field_values(tags, A_COMPOSER, sizeof A_COMPOSER / sizeof *A_COMPOSER, vals, 64);
        st = append_artists(&t->artists, &t->artist_count, vals, n, "composer");
        if (st != MUSICPACK_OK)
            return st;
        n = field_values(tags, A_PERFORMER, sizeof A_PERFORMER / sizeof *A_PERFORMER, vals, 64);
        st = append_artists(&t->artists, &t->artist_count, vals, n, "performer");
        if (st != MUSICPACK_OK)
            return st;
        n = field_values(tags, A_CONDUCTOR, sizeof A_CONDUCTOR / sizeof *A_CONDUCTOR, vals, 64);
        st = append_artists(&t->artists, &t->artist_count, vals, n, "conductor");
        if (st != MUSICPACK_OK)
            return st;
        n = field_values(tags, A_REMIXER, sizeof A_REMIXER / sizeof *A_REMIXER, vals, 64);
        st = append_artists(&t->artists, &t->artist_count, vals, n, "remixer");
        if (st != MUSICPACK_OK)
            return st;
        n = field_values(tags, A_AUTHOR, sizeof A_AUTHOR / sizeof *A_AUTHOR, vals, 64);
        st = append_artists(&t->artists, &t->artist_count, vals, n, "author");
        if (st != MUSICPACK_OK)
            return st;
    }

    st = set_dup(&t->isrc, field_value(tags, A_ISRC, sizeof A_ISRC / sizeof *A_ISRC));
    if (st != MUSICPACK_OK)
        return st;
    if (t->musicbrainz_recording_id == 0) {
        v = field_value(tags, A_MB_RECID, sizeof A_MB_RECID / sizeof *A_MB_RECID);
        if (v == 0)
            v = field_value(tags, A_MB_RECID_LEGACY, sizeof A_MB_RECID_LEGACY / sizeof *A_MB_RECID_LEGACY); /* legacy MB TrackId */
        st = set_dup(&t->musicbrainz_recording_id, v);
        if (st != MUSICPACK_OK)
            return st;
    }
    st = set_dup(&t->musicbrainz_track_id, field_value(tags, A_MB_TRKID, sizeof A_MB_TRKID / sizeof *A_MB_TRKID));
    if (st != MUSICPACK_OK)
        return st;

    if (t->source_store == 0) {
        v = field_value(tags, A_SOURCE, sizeof A_SOURCE / sizeof *A_SOURCE);
        if (v != 0 && *v != '\0' && source_is_store(v)) {
            st = set_dup(&t->source_store, v);
            if (st != MUSICPACK_OK)
                return st;
        }
    }
    st = set_dup(&t->source_track_id, field_value(tags, A_SOURCEID, sizeof A_SOURCEID / sizeof *A_SOURCEID));
    if (st != MUSICPACK_OK)
        return st;

    return MUSICPACK_OK;
}

/* ---- manifest -> APEv2 projection ----------------------------------- */

static musicpack_status
add_text(musicpack_tag_set *out, const char *key, const char *value)
{
    return musicpack_tag_set_add(out, key, value, strlen(value));
}

musicpack_status
musicpack_manifest_to_ape_tags(const musicpack_manifest *m, const musicpack_track *t,
                               int disc, int disc_total, int track_total,
                               musicpack_tag_set *out)
{
    musicpack_status st;
    size_t i;
    char buf[32];

    if (m == 0 || out == 0)
        return MUSICPACK_ERR_INVALID;
    st = musicpack_tag_set_init(out, "apev2");
    if (st != MUSICPACK_OK)
        return st;

    if (m->album_title != 0) {
        st = add_text(out, "Album", m->album_title);
        if (st != MUSICPACK_OK)
            return st;
    }
    for (i = 0; i < m->album_artist_count; i++) {
        st = add_text(out, "Album Artist", m->album_artists[i].name);
        if (st != MUSICPACK_OK)
            return st;
    }
    if (m->release_type != 0) {
        st = add_text(out, "MusicBrainz Album Type", m->release_type);
        if (st != MUSICPACK_OK)
            return st;
    }
    if (m->original_release_date != 0) {
        st = add_text(out, "OriginalDate", m->original_release_date);
        if (st != MUSICPACK_OK)
            return st;
    }
    for (i = 0; i < m->genre_count; i++) {
        st = add_text(out, "Genre", m->genres[i]);
        if (st != MUSICPACK_OK)
            return st;
    }
    if (m->release.release_date != 0) {
        st = add_text(out, "Year", m->release.release_date);
        if (st != MUSICPACK_OK)
            return st;
    }
    if (m->release.country != 0) {
        st = add_text(out, "MusicBrainz Album Country", m->release.country);
        if (st != MUSICPACK_OK)
            return st;
    }
    if (m->release.label != 0) {
        st = add_text(out, "Label", m->release.label);
        if (st != MUSICPACK_OK)
            return st;
    }
    if (m->release.catalogue_number != 0) {
        st = add_text(out, "CatalogNumber", m->release.catalogue_number);
        if (st != MUSICPACK_OK)
            return st;
    }
    if (m->barcode != 0) {
        st = add_text(out, "Barcode", m->barcode);
        if (st != MUSICPACK_OK)
            return st;
    }
    if (m->musicbrainz_release_group_id != 0) {
        st = add_text(out, "MusicBrainz Release Group Id", m->musicbrainz_release_group_id);
        if (st != MUSICPACK_OK)
            return st;
    }
    if (m->musicbrainz_release_id != 0) {
        st = add_text(out, "MusicBrainz Album Id", m->musicbrainz_release_id);
        if (st != MUSICPACK_OK)
            return st;
    }
    if (m->source_type != 0 || m->source_store != 0) {
        st = add_text(out, "Source",
                      m->source_store != 0 ? m->source_store
                                           : (strcmp(m->source_type, "cd-rip") == 0
                                                  ? "CD Rip" : m->source_type));
        if (st != MUSICPACK_OK)
            return st;
    }

    if (t != 0) {
        if (t->title != 0) {
            st = add_text(out, "Title", t->title);
            if (st != MUSICPACK_OK)
                return st;
        }
        for (i = 0; i < t->artist_count; i++) {
            const char *key = "Artist";
            if (t->artists[i].role != 0) {
                if (ci_eq(t->artists[i].role, "composer")) key = "Composer";
                else if (ci_eq(t->artists[i].role, "performer")) key = "Performer";
                else if (ci_eq(t->artists[i].role, "conductor")) key = "Conductor";
                else if (ci_eq(t->artists[i].role, "remixer")) key = "Remixer";
                else if (ci_eq(t->artists[i].role, "author")) key = "Author";
            }
            st = add_text(out, key, t->artists[i].name);
            if (st != MUSICPACK_OK)
                return st;
        }
        if (t->number > 0) {
            if (track_total > 0)
                snprintf(buf, sizeof buf, "%d/%d", t->number, track_total);
            else
                snprintf(buf, sizeof buf, "%d", t->number);
            st = add_text(out, "Track", buf);
            if (st != MUSICPACK_OK)
                return st;
        }
        if (t->isrc != 0) {
            st = add_text(out, "ISRC", t->isrc);
            if (st != MUSICPACK_OK)
                return st;
        }
        if (t->musicbrainz_recording_id != 0) {
            st = add_text(out, "MusicBrainz Recording Id", t->musicbrainz_recording_id);
            if (st != MUSICPACK_OK)
                return st;
        }
        if (t->musicbrainz_track_id != 0) {
            st = add_text(out, "MusicBrainz Release Track Id", t->musicbrainz_track_id);
            if (st != MUSICPACK_OK)
                return st;
        }
        if (t->source_track_id != 0) {
            st = add_text(out, "SourceId", t->source_track_id);
            if (st != MUSICPACK_OK)
                return st;
        }
    }
    if (disc > 0) {
        if (disc_total > 0)
            snprintf(buf, sizeof buf, "%d/%d", disc, disc_total);
        else
            snprintf(buf, sizeof buf, "%d", disc);
        st = add_text(out, "Disc", buf);
        if (st != MUSICPACK_OK)
            return st;
    }
    if (m->source_id != 0 && (t == 0 || t->source_track_id == 0)) {
        st = add_text(out, "SourceId", m->source_id);
        if (st != MUSICPACK_OK)
            return st;
    }

    return MUSICPACK_OK;
}
