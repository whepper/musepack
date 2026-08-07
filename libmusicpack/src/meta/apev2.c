/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD-2-Clause; see include/musicpack/meta.h for the full text.)
*/
/// \file apev2.c
/// APEv2 tag reader and writer.
///
/// Format (as produced by mutagen and mpcenc, both verified byte-for-byte):
///   [header "APETAGEX" (32B, flags 0xA0000000)]
///   items: u32le value_size, u32le flags, key\0, value
///   [footer "APETAGEX" (32B, flags 0x80000000)]
/// Text multi-values are stored as one item with NUL-separated values;
/// binary items (e.g. "Cover Art (Front)") use item flag 0x2.
/// The tag lives at the end of the file; audio bytes are never touched.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <musicpack/meta.h>

#if defined(_WIN32)
# include <io.h>
# define FILE_SEEK(f, o, w) _fseeki64((f), (o), (w))
# define FILE_TELL(f) _ftelli64((f))
# define file_truncate(f, len) (_chsize_s(_fileno((f)), (len)) == 0 ? 0 : -1)
#else
# include <unistd.h>
# define FILE_SEEK(f, o, w) fseeko((f), (o), (w))
# define FILE_TELL(f) ftello((f))
# define file_truncate(f, len) (ftruncate(fileno((f)), (off_t) (len)) == 0 ? 0 : -1)
#endif

#define APE_PREAMBLE "APETAGEX"
/* Flags match mutagen/flac2mpc (the de-facto reference): the footer's
   0x80000000 bit signals "tag has a header"; the header adds the
   "this is a header" bit. mpcenc writes 0xA0000000 headers, which is
   read equivalently by all tools. */
#define APE_HEADER_FLAGS  0xC0000000u  /* HAS_HEADER | IS_HEADER */
#define APE_FOOTER_FLAGS  0x80000000u  /* HAS_HEADER */
#define APE_ITEM_BINARY   0x00000002u

static uint32_t
rd_le32(const unsigned char *b)
{
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) |
           ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
}

static void
wr_le32(unsigned char *b, uint32_t v)
{
    b[0] = (unsigned char) v;
    b[1] = (unsigned char) (v >> 8);
    b[2] = (unsigned char) (v >> 16);
    b[3] = (unsigned char) (v >> 24);
}

musicpack_status
musicpack_ape_read(const char *path, musicpack_tag_set *out)
{
    FILE *f;
    unsigned char footer[32];
    unsigned char pre[8];
    uint32_t version, tag_size, item_count;
    long long file_size, region_start, items_start, items_end;
    size_t region_len;
    unsigned char *region = 0;
    size_t p = 0;
    uint32_t i;
    int has_header = 0;
    musicpack_status st;

    if (path == 0 || out == 0)
        return MUSICPACK_ERR_INVALID;
    st = musicpack_tag_set_init(out, "apev2");
    if (st != MUSICPACK_OK)
        return st;

    f = fopen(path, "rb");
    if (f == 0) {
        musicpack_tag_set_free(out);
        return MUSICPACK_ERR_IO;
    }
    if (FILE_SEEK(f, 0, SEEK_END) != 0) {
        fclose(f);
        musicpack_tag_set_free(out);
        return MUSICPACK_ERR_IO;
    }
    file_size = FILE_TELL(f);
    if (file_size < 32) {
        fclose(f);
        return MUSICPACK_OK; /* too small for a tag */
    }
    if (FILE_SEEK(f, -32, SEEK_END) != 0 || fread(footer, 1, 32, f) != 32) {
        fclose(f);
        musicpack_tag_set_free(out);
        return MUSICPACK_ERR_IO;
    }
    if (memcmp(footer, APE_PREAMBLE, 8) != 0) {
        fclose(f);
        return MUSICPACK_OK; /* no APE tag */
    }
    version = rd_le32(footer + 8);
    tag_size = rd_le32(footer + 12);
    item_count = rd_le32(footer + 16);
    if (version != 1000 && version != 2000) {
        fclose(f);
        musicpack_tag_set_free(out);
        return MUSICPACK_ERR_INVALID;
    }
    if (tag_size < 32 || tag_size > MUSICPACK_METADATA_BLOCK_MAX ||
        (long long) tag_size > file_size) {
        fclose(f);
        musicpack_tag_set_free(out);
        return MUSICPACK_ERR_INVALID;
    }
    region_start = file_size - tag_size;
    items_end = file_size - 32;

    /* A header (when present) opens the tag region with the preamble; this
       also catches the mpcenc/mutagen form whose footer does not set the
       contains-header flag. */
    if (FILE_SEEK(f, region_start, SEEK_SET) != 0 || fread(pre, 1, 8, f) != 8) {
        fclose(f);
        musicpack_tag_set_free(out);
        return MUSICPACK_ERR_IO;
    }
    has_header = memcmp(pre, APE_PREAMBLE, 8) == 0;
    items_start = region_start + (has_header ? 32 : 0);
    if (items_start > items_end) {
        fclose(f);
        musicpack_tag_set_free(out);
        return MUSICPACK_ERR_INVALID;
    }
    region_len = (size_t) (items_end - items_start);
    region = (unsigned char *) malloc(region_len == 0 ? 1 : region_len);
    if (region == 0) {
        fclose(f);
        musicpack_tag_set_free(out);
        return MUSICPACK_ERR_NOMEM;
    }
    if (region_len > 0) {
        if (FILE_SEEK(f, items_start, SEEK_SET) != 0 ||
            fread(region, 1, region_len, f) != region_len) {
            free(region);
            fclose(f);
            musicpack_tag_set_free(out);
            return MUSICPACK_ERR_IO;
        }
    }
    fclose(f);

    for (i = 0; i < item_count && p < region_len; i++) {
        uint32_t vsize, iflags;
        size_t key_start, key_len;

        if (p + 8 > region_len)
            break;
        vsize = rd_le32(region + p);
        iflags = rd_le32(region + p + 4);
        p += 8;
        if ((size_t) vsize > region_len - p)
            break;
        key_start = p;
        while (p < region_len && region[p] != '\0')
            p++;
        if (p >= region_len)
            break; /* unterminated key */
        key_len = p - key_start;
        p++; /* skip key NUL */
        if (key_len == 0 || key_len > MUSICPACK_TAG_KEY_MAX) {
            p += vsize;
            continue;
        }
        {
            char *key = (char *) malloc(key_len + 1);
            if (key == 0) {
                free(region);
                return MUSICPACK_ERR_NOMEM;
            }
            memcpy(key, region + key_start, key_len);
            key[key_len] = '\0';
            if (iflags & APE_ITEM_BINARY) {
                st = musicpack_tag_set_add_binary(out, key, region + p, vsize);
            } else {
                /* NUL-separated text segments become repeated keys */
                size_t seg, seg_start = 0;
                st = MUSICPACK_OK;
                for (seg = 0; seg <= vsize; seg++) {
                    if (seg == vsize || region[p + seg] == '\0') {
                        size_t seg_len = seg - seg_start;
                        if (seg_len > 0) {
                            musicpack_status a =
                                musicpack_tag_set_add(out, key,
                                    (const char *) (region + p + seg_start),
                                    seg_len);
                            if (a == MUSICPACK_ERR_NOMEM) {
                                st = a;
                                break;
                            }
                        }
                        seg_start = seg + 1;
                    }
                }
            }
            free(key);
            if (st != MUSICPACK_OK) {
                free(region);
                return st;
            }
        }
        p += vsize;
    }
    free(region);
    return MUSICPACK_OK;
}

/* ---- writer ------------------------------------------------------- */

static musicpack_status
body_append(unsigned char **body, size_t *len, size_t *cap,
            const char *key, const unsigned char *value, size_t value_len,
            uint32_t flags)
{
    size_t need = 8 + strlen(key) + 1 + value_len;
    size_t newcap;

    if (*len + need > *cap) {
        newcap = *cap == 0 ? 256 : *cap;
        while (newcap < *len + need)
            newcap *= 2;
        {
            unsigned char *nb = (unsigned char *) realloc(*body, newcap);
            if (nb == 0)
                return MUSICPACK_ERR_NOMEM;
            *body = nb;
            *cap = newcap;
        }
    }
    wr_le32(*body + *len, (uint32_t) value_len);
    wr_le32(*body + *len + 4, flags);
    *len += 8;
    memcpy(*body + *len, key, strlen(key) + 1);
    *len += strlen(key) + 1;
    if (value_len > 0)
        memcpy(*body + *len, value, value_len);
    *len += value_len;
    return MUSICPACK_OK;
}

static int
key_eq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char) (ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char) (cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static musicpack_status
build_body(const musicpack_tag_set *tags, unsigned char **body, size_t *body_len,
           size_t *body_cap, uint32_t *item_count)
{
    size_t i;

    *body = 0;
    *body_len = 0;
    *body_cap = 0;
    *item_count = 0;

    if (tags == 0)
        return MUSICPACK_OK;

    for (i = 0; i < tags->count; i++) {
        const musicpack_tag *first = &tags->items[i];
        musicpack_status st;
        size_t k;
        int dup = 0;

        for (k = 0; k < i; k++)
            if (key_eq(tags->items[k].key, first->key)) {
                dup = 1;
                break;
            }
        if (dup)
            continue;

        if (first->is_binary) {
            st = body_append(body, body_len, body_cap,
                             first->key, first->binary,
                             first->binary_len,
                             APE_ITEM_BINARY);
            if (st != MUSICPACK_OK)
                return st;
        } else {
            /* join all text values for this key with NUL separators */
            size_t total = 0, segs = 0, j, o = 0;
            unsigned char *buf;

            for (j = 0; j < tags->count; j++)
                if (!tags->items[j].is_binary &&
                    key_eq(tags->items[j].key, first->key)) {
                    total += tags->items[j].value_len;
                    segs++;
                }
            buf = (unsigned char *) malloc(total + segs);
            if (buf == 0)
                return MUSICPACK_ERR_NOMEM;
            for (j = 0; j < tags->count; j++)
                if (!tags->items[j].is_binary &&
                    key_eq(tags->items[j].key, first->key)) {
                    if (tags->items[j].value_len > 0) {
                        memcpy(buf + o, tags->items[j].value,
                               tags->items[j].value_len);
                        o += tags->items[j].value_len;
                    }
                    if (o < total + segs - 1)
                        buf[o++] = '\0';
                }
            st = body_append(body, body_len, body_cap, first->key, buf, o, 0);
            free(buf);
            if (st != MUSICPACK_OK)
                return st;
        }
        (*item_count)++;
    }
    return MUSICPACK_OK;
}

musicpack_status
musicpack_ape_write(const char *path, const musicpack_tag_set *tags)
{
    unsigned char *body = 0;
    size_t body_len = 0, body_cap = 0;
    uint32_t item_count = 0;
    uint32_t tag_size;
    unsigned char header[32], footer[32];
    FILE *f;
    musicpack_status st;

    if (path == 0)
        return MUSICPACK_ERR_INVALID;
    st = build_body(tags, &body, &body_len, &body_cap, &item_count);
    if (st != MUSICPACK_OK)
        return st;

    f = fopen(path, "r+b");
    if (f == 0) {
        free(body);
        return MUSICPACK_ERR_IO;
    }

    /* remove an existing APE tag (footer probe at EOF) */
    if (FILE_SEEK(f, 0, SEEK_END) == 0) {
        long long sz = FILE_TELL(f);
        if (sz >= 32 && FILE_SEEK(f, -32, SEEK_END) == 0) {
            unsigned char probe[32];
            if (fread(probe, 1, 32, f) == 32 && memcmp(probe, APE_PREAMBLE, 8) == 0) {
                uint32_t v = rd_le32(probe + 8);
                uint32_t ts = rd_le32(probe + 12);
                if ((v == 1000 || v == 2000) && ts >= 32 && (long long) ts <= sz) {
                    if (FILE_SEEK(f, sz - ts, SEEK_SET) == 0)
                        file_truncate(f, sz - ts);
                }
            }
        }
    }

    if (item_count == 0) {
        free(body);
        fclose(f);
        return MUSICPACK_OK;
    }

    /* The size field counts items + footer only (the header is not
       included); this is the mutagen/mpcenc convention. */
    tag_size = (uint32_t) (32 + body_len);
    memset(header, 0, sizeof header);
    memcpy(header, APE_PREAMBLE, 8);
    wr_le32(header + 8, 2000);
    wr_le32(header + 12, tag_size);
    wr_le32(header + 16, item_count);
    wr_le32(header + 20, APE_HEADER_FLAGS);
    memcpy(footer, header, sizeof footer);
    wr_le32(footer + 20, APE_FOOTER_FLAGS);

    if (FILE_SEEK(f, 0, SEEK_END) != 0 ||
        fwrite(header, 1, 32, f) != 32 ||
        (body_len > 0 && fwrite(body, 1, body_len, f) != body_len) ||
        fwrite(footer, 1, 32, f) != 32) {
        free(body);
        fclose(f);
        return MUSICPACK_ERR_IO;
    }
    free(body);
    if (fclose(f) != 0)
        return MUSICPACK_ERR_IO;
    return MUSICPACK_OK;
}
