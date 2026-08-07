/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD-2-Clause; see include/musicpack/meta.h for the full text.)
*/
/// \file mb.c
/// MusicBrainz release-document enrichment and match confidence.
///
/// Accepts either a release lookup response or a search envelope
/// ({"releases": [...]}) and fills empty .mpack manifest fields from the
/// first release, first-wins per field: existing source/local values are
/// never overwritten. The response is untrusted: cJSON nesting is bounded
/// and the document size is capped by the caller's fetch and here.

#include <stdlib.h>
#include <string.h>

#include "../internal.h"
#include <musicpack/meta.h>

#define MB_DOC_MAX (8u * 1024u * 1024u)

static cJSON *
select_release(cJSON *root)
{
    cJSON *v;
    if (root == 0)
        return 0;
    v = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsString(v))
        return root;
    v = cJSON_GetObjectItemCaseSensitive(root, "releases");
    if (cJSON_IsArray(v) && cJSON_GetArraySize(v) > 0)
        return cJSON_GetArrayItem(v, 0);
    return 0;
}

static const char *
jstr(cJSON *o, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return (v != 0 && cJSON_IsString(v)) ? v->valuestring : 0;
}

/* Sets *dst from v when empty. Returns -1 on NOMEM, 0 on no-op, 1 on set. */
static int
set_empty(char **dst, const char *v, musicpack_status *st)
{
    if (*dst != 0 || v == 0 || *v == '\0')
        return 0;
    *dst = strdup(v);
    if (*dst == 0) {
        *st = MUSICPACK_ERR_NOMEM;
        return -1;
    }
    return 1;
}

#define SET_EMPTY(dst, src) do { if (set_empty(&(dst), (src), &st) < 0) goto done; } while (0)

static musicpack_track *
find_track(musicpack_manifest *m, int disc_num, int track_num)
{
    size_t d;
    for (d = 0; d < m->disc_count; d++) {
        size_t t;
        if (m->discs[d].disc != disc_num)
            continue;
        for (t = 0; t < m->discs[d].track_count; t++)
            if (m->discs[d].tracks[t].number == track_num)
                return &m->discs[d].tracks[t];
    }
    return 0;
}

musicpack_status
musicpack_mb_apply_release(const char *mb_json, musicpack_manifest *m)
{
    cJSON *root, *rel, *rg, *item;
    musicpack_status st = MUSICPACK_OK;
    const char *s;

    if (mb_json == 0 || m == 0)
        return MUSICPACK_ERR_INVALID;
    if (strlen(mb_json) > MB_DOC_MAX)
        return MUSICPACK_ERR_INVALID;
    root = cJSON_ParseWithLength(mb_json, strlen(mb_json));
    if (root == 0)
        return MUSICPACK_ERR_INVALID;
    rel = select_release(root);
    if (rel == 0) {
        cJSON_Delete(root);
        return MUSICPACK_ERR_INVALID;
    }

    SET_EMPTY(m->musicbrainz_release_id, jstr(rel, "id"));
    SET_EMPTY(m->album_title, jstr(rel, "title"));
    SET_EMPTY(m->release.release_date, jstr(rel, "date"));
    SET_EMPTY(m->release.country, jstr(rel, "country"));
    SET_EMPTY(m->barcode, jstr(rel, "barcode"));

    rg = cJSON_GetObjectItemCaseSensitive(rel, "release-group");
    if (cJSON_IsObject(rg)) {
        SET_EMPTY(m->musicbrainz_release_group_id, jstr(rg, "id"));
        s = jstr(rg, "primary-type");
        if (s != 0 && m->release_type == 0) {
            const char *rt = musicpack_meta_release_type_from_tag(s);
            m->release_type = strdup(rt != 0 ? rt : "other");
            if (m->release_type == 0) {
                st = MUSICPACK_ERR_NOMEM;
                goto done;
            }
        }
        SET_EMPTY(m->original_release_date, jstr(rg, "first-release-date"));
    }

    if (m->album_artist_count == 0) {
        item = cJSON_GetObjectItemCaseSensitive(rel, "artist-credit");
        if (cJSON_IsArray(item)) {
            int n = cJSON_GetArraySize(item);
            if (n > 0) {
                int k = 0;
                cJSON *cr;
                m->album_artists =
                    (musicpack_artist *) calloc((size_t) n, sizeof *m->album_artists);
                if (m->album_artists == 0) {
                    st = MUSICPACK_ERR_NOMEM;
                    goto done;
                }
                cJSON_ArrayForEach(cr, item) {
                    const char *nm = jstr(cr, "name");
                    if (nm == 0)
                        continue;
                    m->album_artists[k].name = strdup(nm);
                    m->album_artists[k].role = strdup("main");
                    if (m->album_artists[k].name == 0 || m->album_artists[k].role == 0) {
                        st = MUSICPACK_ERR_NOMEM;
                        goto done;
                    }
                    k++;
                }
                m->album_artist_count = (size_t) k;
            }
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(rel, "labels");
    if (cJSON_IsArray(item) && cJSON_GetArraySize(item) > 0) {
        cJSON *l0 = cJSON_GetArrayItem(item, 0);
        cJSON *lab;
        if (cJSON_IsObject(l0)) {
            lab = cJSON_GetObjectItemCaseSensitive(l0, "label");
            if (cJSON_IsObject(lab))
                SET_EMPTY(m->release.label, jstr(lab, "name"));
            SET_EMPTY(m->release.catalogue_number, jstr(l0, "catalog-number"));
        }
    }
    if (m->release.release_date != 0 || m->release.country != 0 ||
        m->release.label != 0 || m->release.catalogue_number != 0 ||
        m->release.edition != 0 || m->release.notes != 0)
        m->release.present = 1;

    item = cJSON_GetObjectItemCaseSensitive(rel, "media");
    if (cJSON_IsArray(item)) {
        cJSON *mi;
        cJSON_ArrayForEach(mi, item) {
            int pos = 0;
            cJSON *p = cJSON_GetObjectItemCaseSensitive(mi, "position");
            cJSON *tracks;
            if (cJSON_IsNumber(p))
                pos = (int) p->valuedouble;
            s = jstr(mi, "format");
            if (pos > 0) {
                size_t d;
                for (d = 0; d < m->disc_count; d++)
                    if (m->discs[d].disc == pos && m->discs[d].format == 0 && s != 0) {
                        m->discs[d].format = strdup(s);
                        if (m->discs[d].format == 0) {
                            st = MUSICPACK_ERR_NOMEM;
                            goto done;
                        }
                        break;
                    }
            }
            tracks = cJSON_GetObjectItemCaseSensitive(mi, "tracks");
            if (!cJSON_IsArray(tracks))
                continue;
            {
                cJSON *tr;
                cJSON_ArrayForEach(tr, tracks) {
                    int num = 0;
                    cJSON *nobj = cJSON_GetObjectItemCaseSensitive(tr, "number");
                    musicpack_track *mt;
                    cJSON *rec;
                    if (cJSON_IsString(nobj))
                        musicpack_meta_parse_track_number(nobj->valuestring, &num);
                    if (num <= 0)
                        continue;
                    mt = find_track(m, pos, num);
                    if (mt == 0)
                        continue;
                    SET_EMPTY(mt->musicbrainz_track_id, jstr(tr, "id"));
                    SET_EMPTY(mt->title, jstr(tr, "title"));
                    rec = cJSON_GetObjectItemCaseSensitive(tr, "recording");
                    if (cJSON_IsObject(rec)) {
                        cJSON *isrcs;
                        SET_EMPTY(mt->musicbrainz_recording_id, jstr(rec, "id"));
                        if (mt->isrc == 0) {
                            isrcs = cJSON_GetObjectItemCaseSensitive(rec, "isrcs");
                            if (cJSON_IsArray(isrcs) && cJSON_GetArraySize(isrcs) > 0) {
                                cJSON *i0 = cJSON_GetArrayItem(isrcs, 0);
                                if (cJSON_IsString(i0))
                                    SET_EMPTY(mt->isrc, i0->valuestring);
                            }
                        }
                    }
                }
            }
        }
    }

done:
    cJSON_Delete(root);
    return st;
}

const char *
musicpack_mb_match_confidence(const char *mb_json, const musicpack_manifest *m)
{
    cJSON *root, *rel, *media;
    const char *id, *barcode, *title;
    int bc = 0, isrc_hit = 0, count_ok = 0, title_match = 0;
    size_t total = 0, d, t;

    if (mb_json == 0 || m == 0 || strlen(mb_json) > MB_DOC_MAX)
        return "none";
    root = cJSON_ParseWithLength(mb_json, strlen(mb_json));
    if (root == 0)
        return "none";
    rel = select_release(root);
    if (rel == 0) {
        cJSON_Delete(root);
        return "none";
    }

    id = jstr(rel, "id");
    if (m->musicbrainz_release_id != 0 && id != 0 &&
        strcmp(m->musicbrainz_release_id, id) == 0) {
        cJSON_Delete(root);
        return "exact";
    }

    barcode = jstr(rel, "barcode");
    if (m->barcode != 0 && barcode != 0 && *barcode != '\0' &&
        strcmp(m->barcode, barcode) == 0)
        bc = 1;
    title = jstr(rel, "title");

    for (d = 0; d < m->disc_count; d++)
        for (t = 0; t < m->discs[d].track_count; t++)
            total++;
    media = cJSON_GetObjectItemCaseSensitive(rel, "media");
    if (cJSON_IsArray(media)) {
        int mb_tracks = 0;
        cJSON *mi;
        cJSON_ArrayForEach(mi, media) {
            cJSON *trs = cJSON_GetObjectItemCaseSensitive(mi, "tracks");
            if (cJSON_IsArray(trs)) {
                cJSON *tr;
                mb_tracks += cJSON_GetArraySize(trs);
                cJSON_ArrayForEach(tr, trs) {
                    cJSON *rec, *isrcs;
                    rec = cJSON_GetObjectItemCaseSensitive(tr, "recording");
                    if (!cJSON_IsObject(rec))
                        continue;
                    isrcs = cJSON_GetObjectItemCaseSensitive(rec, "isrcs");
                    if (!cJSON_IsArray(isrcs))
                        continue;
                    {
                        cJSON *ir;
                        cJSON_ArrayForEach(ir, isrcs) {
                            if (!cJSON_IsString(ir))
                                continue;
                            for (d = 0; d < m->disc_count && !isrc_hit; d++)
                                for (t = 0; t < m->discs[d].track_count && !isrc_hit; t++)
                                    if (m->discs[d].tracks[t].isrc != 0 &&
                                        strcmp(m->discs[d].tracks[t].isrc,
                                               ir->valuestring) == 0)
                                        isrc_hit = 1;
                        }
                    }
                }
            }
        }
        count_ok = mb_tracks > 0 && (size_t) mb_tracks == total;
    }
    /* title is read before the tree is freed */
    title_match = title != 0 && m->album_title != 0 &&
                  strcmp(title, m->album_title) == 0;
    cJSON_Delete(root);

    if (bc)
        return "confirmed";
    if (isrc_hit && count_ok)
        return "confirmed";
    if (isrc_hit || title_match)
        return "probable";
    return "none";
}
