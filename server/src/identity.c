/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see identity.h)
*/
#include "identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <musicpack/checksum.h>

/* ---------- small helpers ---------- */

static musicpack_status
hash_text(const char *s, size_t len, char *out, size_t cap)
{
    return musicpack_sha256(s, len, out, cap);
}

/* Appends "\nkey=value" for each present string member (used to canonicalise
   a set of fields for hashing). Returns 1 if the value was present. */
static int
append_field(char *buf, size_t cap, size_t *len, const char *key,
             const char *value)
{
    int k;
    if (value == 0)
        return 0;
    k = snprintf(buf + *len, cap - *len, "%s=%s\n", key, value);
    if (k < 0 || (size_t) k >= cap - *len)
        return 0;
    *len += (size_t) k;
    return 1;
}

/* Sorts album artists by (name, role) so a reordered artist list does not
   change identity; roles are part of the identity. */
static void
sort_artists(musicpack_artist *a, size_t n)
{
    size_t i, j;
    for (i = 1; i < n; i++) {
        musicpack_artist key = a[i];
        j = i;
        while (j > 0) {
            int c = strcmp(a[j - 1].name, key.name);
            if (c == 0 && a[j - 1].role != 0 && key.role != 0)
                c = strcmp(a[j - 1].role, key.role);
            if (c <= 0)
                break;
            a[j] = a[j - 1];
            j--;
        }
        a[j] = key;
    }
}

static void
append_artists(char *buf, size_t cap, size_t *len,
               const musicpack_artist *a, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        char line[2048];
        int k = snprintf(line, sizeof line, "artist=%s\nrole=%s\n",
                         a[i].name, a[i].role != 0 ? a[i].role : "");
        if (k > 0 && (size_t) k < sizeof line &&
            (size_t) k < cap - *len) {
            memcpy(buf + *len, line, (size_t) k);
            *len += (size_t) k;
        }
    }
}

/* ---------- public API ---------- */

musicpack_status
mp_identity_manifest_hash(const char *json, size_t len, char *out, size_t cap)
{
    return musicpack_sha256(json, len, out, cap);
}

musicpack_status
mp_identity_package_fingerprint(const musicpack_manifest *m, char *out,
                                size_t cap)
{
    char *json = 0;
    musicpack_status s = musicpack_manifest_write(m, &json);
    if (s != MUSICPACK_OK)
        return s;
    s = musicpack_sha256(json, strlen(json), out, cap);
    free(json);
    return s;
}

musicpack_status
mp_identity_group_key(const musicpack_manifest *m, char *out, size_t cap)
{
    musicpack_artist *copy;
    size_t len = 0;
    char buf[8192];

    if (m->musicbrainz_release_group_id != 0 &&
        *m->musicbrainz_release_group_id != '\0') {
        snprintf(out, cap, "mb:%s", m->musicbrainz_release_group_id);
        return MUSICPACK_OK;
    }
    copy = (musicpack_artist *) calloc(m->album_artist_count,
                                       sizeof *copy);
    if (m->album_artist_count > 0 && copy == 0)
        return MUSICPACK_ERR_NOMEM;
    if (m->album_artist_count > 0) {
        size_t i;
        for (i = 0; i < m->album_artist_count; i++) {
            copy[i].name = m->album_artists[i].name;
            copy[i].role = m->album_artists[i].role;
        }
    }
    sort_artists(copy, m->album_artist_count);
    append_field(buf, sizeof buf, &len, "title", m->album_title);
    append_field(buf, sizeof buf, &len, "date",
                 m->original_release_date);
    append_field(buf, sizeof buf, &len, "type", m->release_type);
    append_artists(buf, sizeof buf, &len, copy, m->album_artist_count);
    free(copy);
    snprintf(out, cap, "h:");
    return hash_text(buf, len, out + 2, cap - 2);
}

musicpack_status
mp_identity_release_key(const musicpack_manifest *m, char *out, size_t cap)
{
    size_t len = 0;
    char buf[8192];

    if (m->musicbrainz_release_id != 0 &&
        *m->musicbrainz_release_id != '\0') {
        snprintf(out, cap, "mb:%s", m->musicbrainz_release_id);
        return MUSICPACK_OK;
    }
    append_field(buf, sizeof buf, &len, "edition", m->release.edition);
    append_field(buf, sizeof buf, &len, "date", m->release.release_date);
    append_field(buf, sizeof buf, &len, "country", m->release.country);
    append_field(buf, sizeof buf, &len, "label", m->release.label);
    append_field(buf, sizeof buf, &len, "catalogue",
                 m->release.catalogue_number);
    append_field(buf, sizeof buf, &len, "barcode", m->barcode);
    snprintf(out, cap, "h:");
    return hash_text(buf, len, out + 2, cap - 2);
}
