/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD-2-Clause; see include/musicpack/meta.h for the full text.)
*/
/// \file flac.c
/// Read-only FLAC metadata-block walker: skips STREAMINFO, parses the
/// VORBIS_COMMENT block and captures PICTURE blocks. Audio frames are never
/// touched. All lengths are bounded and EOF-checked; the file is untrusted.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <musicpack/meta.h>

#define FLAC_BLOCK_MAX 4096

static uint32_t
rd_be32(const unsigned char *b)
{
    return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) |
           ((uint32_t) b[2] << 8) | (uint32_t) b[3];
}

static int
read_exact(FILE *f, void *dst, size_t n)
{
    return fread(dst, 1, n, f) == n;
}

/* Portable skip: read-and-discard (avoids fseeko on Windows). */
static int
skip_bytes(FILE *f, size_t n)
{
    char junk[65536];
    while (n > 0) {
        size_t chunk = n > sizeof junk ? sizeof junk : n;
        if (fread(junk, 1, chunk, f) != chunk)
            return 0;
        n -= chunk;
    }
    return 1;
}

static void
pictures_free_items(musicpack_pictures *p)
{
    size_t i;
    for (i = 0; i < p->count; i++) {
        free(p->items[i].mime);
        free(p->items[i].description);
        free(p->items[i].data);
    }
}

void
musicpack_pictures_free(musicpack_pictures *p)
{
    if (p == 0)
        return;
    pictures_free_items(p);
    free(p->items);
    memset(p, 0, sizeof *p);
}

static musicpack_status
pictures_add(musicpack_pictures *p, const musicpack_picture *pic)
{
    size_t newcap;
    musicpack_picture *ni;

    if (p->count == p->cap) {
        newcap = p->cap == 0 ? 4 : p->cap * 2;
        ni = (musicpack_picture *) realloc(p->items, newcap * sizeof *ni);
        if (ni == 0)
            return MUSICPACK_ERR_NOMEM;
        p->items = ni;
        p->cap = newcap;
    }
    p->items[p->count] = *pic;
    p->count++;
    return MUSICPACK_OK;
}

/* Parses a FLAC PICTURE block payload; on failure the picture is unchanged. */
static musicpack_status
picture_parse(const unsigned char *b, size_t len, musicpack_picture *pic)
{
    size_t p = 0;
    uint32_t mlen, dlen, w, h, depth, colors, datalen;
    unsigned char *data;

    if (len < 32)
        return MUSICPACK_ERR_INVALID;
    pic->type = (int) rd_be32(b);
    p = 4;
    mlen = rd_be32(b + p);
    p += 4;
    if (mlen > 128 || p + mlen > len)
        return MUSICPACK_ERR_INVALID;
    pic->mime = (char *) malloc((size_t) mlen + 1);
    if (pic->mime == 0)
        return MUSICPACK_ERR_NOMEM;
    memcpy(pic->mime, b + p, mlen);
    pic->mime[mlen] = '\0';
    p += mlen;

    dlen = rd_be32(b + p);
    p += 4;
    if (dlen > 4096 || p + dlen > len)
        goto bad_mime;
    pic->description = (char *) malloc((size_t) dlen + 1);
    if (pic->description == 0) {
        free(pic->mime);
        pic->mime = 0;
        return MUSICPACK_ERR_NOMEM;
    }
    memcpy(pic->description, b + p, dlen);
    pic->description[dlen] = '\0';
    p += dlen;

    if (p + 16 > len)
        goto bad_desc;
    w = rd_be32(b + p); h = rd_be32(b + p + 4);
    depth = rd_be32(b + p + 8); colors = rd_be32(b + p + 12);
    p += 16;
    datalen = rd_be32(b + p);
    p += 4;
    if (datalen > MUSICPACK_PICTURE_MAX || p + datalen > len)
        goto bad_desc;

    data = (unsigned char *) malloc(datalen == 0 ? 1 : datalen);
    if (data == 0) {
        free(pic->description);
        pic->description = 0;
        free(pic->mime);
        pic->mime = 0;
        return MUSICPACK_ERR_NOMEM;
    }
    if (datalen > 0)
        memcpy(data, b + p, datalen);

    pic->width = (int) w;
    pic->height = (int) h;
    pic->depth = (int) depth;
    pic->colors = (int) colors;
    pic->data_len = datalen;
    pic->data = data;
    return MUSICPACK_OK;

bad_desc:
    free(pic->description);
    pic->description = 0;
bad_mime:
    free(pic->mime);
    pic->mime = 0;
    return MUSICPACK_ERR_INVALID;
}

musicpack_status
musicpack_flac_read_metadata(const char *path, musicpack_tag_set *comments,
                             musicpack_pictures *pictures)
{
    FILE *f;
    unsigned char sig[4];
    musicpack_status st = MUSICPACK_OK;
    unsigned blocks = 0;

    if (path == 0)
        return MUSICPACK_ERR_INVALID;
    if (comments != 0) {
        st = musicpack_tag_set_init(comments, "flac");
        if (st != MUSICPACK_OK)
            return st;
    }
    if (pictures != 0)
        memset(pictures, 0, sizeof *pictures);

    f = fopen(path, "rb");
    if (f == 0) {
        if (comments != 0)
            musicpack_tag_set_free(comments);
        return MUSICPACK_ERR_IO;
    }

    if (!read_exact(f, sig, sizeof sig) || memcmp(sig, "fLaC", 4) != 0) {
        st = MUSICPACK_ERR_INVALID;
        goto done;
    }

    for (;;) {
        unsigned char hdr[4];
        unsigned type;
        uint32_t blen;
        unsigned is_last;

        if (++blocks > FLAC_BLOCK_MAX) {
            st = MUSICPACK_ERR_INVALID;
            goto done;
        }
        if (!read_exact(f, hdr, sizeof hdr)) {
            st = MUSICPACK_ERR_INVALID;
            goto done;
        }
        is_last = (hdr[0] >> 7) & 1;
        type = hdr[0] & 0x7f;
        blen = ((uint32_t) hdr[1] << 16) | ((uint32_t) hdr[2] << 8) | hdr[3];
        if (blen > MUSICPACK_METADATA_BLOCK_MAX) {
            st = MUSICPACK_ERR_INVALID;
            goto done;
        }

        if (type == 4 && comments != 0) {
            unsigned char *buf = (unsigned char *) malloc(blen == 0 ? 1 : blen);
            if (buf == 0) {
                st = MUSICPACK_ERR_NOMEM;
                goto done;
            }
            if (blen > 0 && !read_exact(f, buf, blen)) {
                free(buf);
                st = MUSICPACK_ERR_INVALID;
                goto done;
            }
            st = musicpack_vorbis_parse(buf, blen, comments);
            free(buf);
            if (st != MUSICPACK_OK)
                goto done;
        } else if (type == 6 && pictures != 0) {
            unsigned char *buf = (unsigned char *) malloc(blen == 0 ? 1 : blen);
            musicpack_picture pic;
            if (buf == 0) {
                st = MUSICPACK_ERR_NOMEM;
                goto done;
            }
            if (blen > 0 && !read_exact(f, buf, blen)) {
                free(buf);
                st = MUSICPACK_ERR_INVALID;
                goto done;
            }
            memset(&pic, 0, sizeof pic);
            st = picture_parse(buf, blen, &pic);
            free(buf);
            if (st != MUSICPACK_OK)
                goto done;
            st = pictures_add(pictures, &pic);
            if (st != MUSICPACK_OK) {
                free(pic.mime);
                free(pic.description);
                free(pic.data);
                goto done;
            }
        } else {
            if (!skip_bytes(f, blen)) {
                st = MUSICPACK_ERR_INVALID;
                goto done;
            }
        }

        if (is_last)
            break;
    }
    st = MUSICPACK_OK;

done:
    fclose(f);
    if (st != MUSICPACK_OK) {
        if (comments != 0)
            musicpack_tag_set_free(comments);
        if (pictures != 0)
            musicpack_pictures_free(pictures);
    }
    return st;
}
