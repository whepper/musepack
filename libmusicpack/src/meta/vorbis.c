/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD-2-Clause; see include/musicpack/meta.h for the full text.)
*/
/// \file vorbis.c
/// Read-only Vorbis Comment parser.
///
/// The structure (as stored in a FLAC VORBIS_COMMENT block) is:
///   u32le vendor_length, vendor bytes,
///   u32le count,
///   u32le length + "KEY=value" bytes, repeated.
/// There is no trailing framing bit in the FLAC container form.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <musicpack/meta.h>

static uint32_t
rd_le32(const unsigned char *b)
{
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) |
           ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
}

musicpack_status
musicpack_vorbis_parse(const unsigned char *data, size_t len, musicpack_tag_set *out)
{
    size_t p = 0;
    uint32_t vlen, count, i;

    if (data == 0 || out == 0)
        return MUSICPACK_ERR_INVALID;
    if (len < 8)
        return MUSICPACK_ERR_INVALID;

    /* vendor string */
    vlen = rd_le32(data);
    p = 4;
    if ((size_t) vlen > len - p)
        return MUSICPACK_ERR_INVALID;
    p += vlen;

    /* user comments */
    if (p + 4 > len)
        return MUSICPACK_ERR_INVALID;
    count = rd_le32(data + p);
    p += 4;
    if (count > MUSICPACK_TAG_COUNT_MAX)
        return MUSICPACK_ERR_INVALID;

    for (i = 0; i < count; i++) {
        uint32_t clen;
        const unsigned char *field;
        const unsigned char *eq;
        musicpack_status st;

        if (p + 4 > len)
            return MUSICPACK_ERR_INVALID;
        clen = rd_le32(data + p);
        p += 4;
        if ((size_t) clen > len - p || clen > MUSICPACK_TAG_VALUE_MAX)
            return MUSICPACK_ERR_INVALID;
        field = data + p;
        p += clen;

        /* entries without '=' carry no key; skip them (harmless). */
        eq = (const unsigned char *) memchr(field, '=', clen);
        if (eq == 0)
            continue;
        {
            size_t key_len = (size_t) (eq - field);
            size_t value_len = clen - key_len - 1;
            const unsigned char *value = eq + 1;
            char *key = (char *) malloc(key_len + 1);

            if (key == 0)
                return MUSICPACK_ERR_NOMEM;
            memcpy(key, field, key_len);
            key[key_len] = '\0';

            /* Skip malformed values rather than failing the whole file. */
            st = musicpack_tag_set_add(out, key, (const char *) value, value_len);
            free(key);
            if (st != MUSICPACK_OK && st != MUSICPACK_ERR_INVALID)
                return st;
        }
    }
    return MUSICPACK_OK;
}

/* Reads up to max bytes of a file into a NUL-terminated buffer. */
static char *
read_file_bounded(const char *path, size_t max, size_t *out_len,
                  musicpack_status *status)
{
    FILE *f;
    long len;
    char *buf;

    f = fopen(path, "rb");
    if (f == 0) {
        *status = MUSICPACK_ERR_IO;
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        *status = MUSICPACK_ERR_IO;
        return 0;
    }
    len = ftell(f);
    if (len < 0 || (size_t) len > max) {
        fclose(f);
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        *status = MUSICPACK_ERR_IO;
        return 0;
    }
    buf = (char *) malloc((size_t) len + 1);
    if (buf == 0) {
        fclose(f);
        *status = MUSICPACK_ERR_NOMEM;
        return 0;
    }
    if (len > 0 && fread(buf, 1, (size_t) len, f) != (size_t) len) {
        free(buf);
        fclose(f);
        *status = MUSICPACK_ERR_IO;
        return 0;
    }
    fclose(f);
    buf[len] = '\0';
    *out_len = (size_t) len;
    *status = MUSICPACK_OK;
    return buf;
}

musicpack_status
musicpack_vorbis_read(const char *path, musicpack_tag_set *out)
{
    musicpack_status st;
    char *buf;
    size_t len;

    if (path == 0 || out == 0)
        return MUSICPACK_ERR_INVALID;
    buf = read_file_bounded(path, MUSICPACK_TAG_VALUE_MAX + 16, &len, &st);
    if (buf == 0)
        return st;
    st = musicpack_vorbis_parse((const unsigned char *) buf, len, out);
    free(buf);
    return st;
}
