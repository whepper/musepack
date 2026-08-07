/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD-2-Clause; see include/musicpack/meta.h for the full text.)
*/
/// \file tag.c
/// musicpack_tag_set model: bounded, UTF-8-safe multi-value tag storage.

#include <stdlib.h>
#include <string.h>

#include <musicpack/meta.h>

static int
ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}

/* ASCII case-insensitive equality for tag keys. */
static int
key_eq(const char *a, const char *b)
{
    if (a == 0 || b == 0)
        return a == b;
    while (*a != '\0' && *b != '\0') {
        if (ascii_tolower((unsigned char) *a) != ascii_tolower((unsigned char) *b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int
musicpack_utf8_valid(const unsigned char *s, size_t len)
{
    size_t i = 0;

    if (s == 0)
        return len == 0;
    while (i < len) {
        unsigned char c = s[i];
        if (c < 0x80) {
            i++;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 >= len || (s[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 >= len || (s[i + 1] & 0xC0) != 0x80 ||
                (s[i + 2] & 0xC0) != 0x80)
                return 0;
            if (c == 0xE0 && s[i + 1] < 0xA0)     /* overlong */
                return 0;
            if (c == 0xED && s[i + 1] >= 0xA0)    /* surrogate */
                return 0;
            i += 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 >= len || (s[i + 1] & 0xC0) != 0x80 ||
                (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80)
                return 0;
            if (c == 0xF0 && s[i + 1] < 0x90)     /* overlong */
                return 0;
            if (c == 0xF4 && s[i + 1] >= 0x90)    /* > U+10FFFF */
                return 0;
            i += 4;
        } else {
            return 0;                             /* continuation / bad lead */
        }
    }
    return 1;
}

/* Key policy: non-empty, printable (no control chars), length-bounded. */
static int
key_valid(const char *key)
{
    const unsigned char *p = (const unsigned char *) key;
    size_t n = 0;

    if (key == 0 || *key == '\0')
        return 0;
    while (*p != '\0') {
        if (*p < 0x20 || *p == 0x7f)
            return 0;
        n++;
        p++;
    }
    return n <= MUSICPACK_TAG_KEY_MAX;
}

static musicpack_status
tag_set_reserve(musicpack_tag_set *s, size_t need)
{
    size_t newcap;
    musicpack_tag *ni;

    if (need <= s->cap)
        return MUSICPACK_OK;
    if (need > MUSICPACK_TAG_COUNT_MAX)
        return MUSICPACK_ERR_INVALID;
    newcap = s->cap == 0 ? 16 : s->cap;
    while (newcap < need)
        newcap *= 2;
    ni = (musicpack_tag *) realloc(s->items, newcap * sizeof *ni);
    if (ni == 0)
        return MUSICPACK_ERR_NOMEM;
    s->items = ni;
    s->cap = newcap;
    return MUSICPACK_OK;
}

musicpack_status
musicpack_tag_set_init(musicpack_tag_set *s, const char *source)
{
    if (s == 0)
        return MUSICPACK_ERR_INVALID;
    memset(s, 0, sizeof *s);
    if (source != 0) {
        s->source = strdup(source);
        if (s->source == 0)
            return MUSICPACK_ERR_NOMEM;
    }
    return MUSICPACK_OK;
}

void
musicpack_tag_set_free(musicpack_tag_set *s)
{
    size_t i;

    if (s == 0)
        return;
    for (i = 0; i < s->count; i++) {
        free(s->items[i].key);
        free(s->items[i].value);
        free(s->items[i].binary);
    }
    free(s->items);
    free(s->source);
    memset(s, 0, sizeof *s);
}

static musicpack_status
tag_set_add_impl(musicpack_tag_set *s, const char *key, const char *value,
                 size_t value_len, const unsigned char *binary, size_t binary_len)
{
    musicpack_tag *t;
    musicpack_status st;

    if (s == 0 || !key_valid(key))
        return MUSICPACK_ERR_INVALID;
    st = tag_set_reserve(s, s->count + 1);
    if (st != MUSICPACK_OK)
        return st;
    if (value != 0 && value_len > MUSICPACK_TAG_VALUE_MAX)
        return MUSICPACK_ERR_INVALID;
    if (binary != 0 && binary_len > MUSICPACK_TAG_VALUE_MAX)
        return MUSICPACK_ERR_INVALID;

    t = &s->items[s->count];
    memset(t, 0, sizeof *t);
    t->key = strdup(key);
    if (t->key == 0)
        return MUSICPACK_ERR_NOMEM;
    if (binary != 0) {
        t->is_binary = 1;
        t->binary = (unsigned char *) malloc(binary_len == 0 ? 1 : binary_len);
        if (t->binary == 0) {
            free(t->key);
            t->key = 0;
            return MUSICPACK_ERR_NOMEM;
        }
        if (binary_len > 0)
            memcpy(t->binary, binary, binary_len);
        t->binary_len = binary_len;
    } else {
        char *copy = (char *) malloc(value_len + 1);
        if (copy == 0) {
            free(t->key);
            t->key = 0;
            return MUSICPACK_ERR_NOMEM;
        }
        memcpy(copy, value, value_len);
        copy[value_len] = '\0';
        t->value = copy;
        t->value_len = value_len;
    }
    s->count++;
    return MUSICPACK_OK;
}

musicpack_status
musicpack_tag_set_add(musicpack_tag_set *s, const char *key, const char *value,
                      size_t value_len)
{
    if (value == 0 || value_len > MUSICPACK_TAG_VALUE_MAX)
        return MUSICPACK_ERR_INVALID;
    /* Embedded NULs are malformed; keep the text up to the first NUL. */
    {
        const unsigned char *p = (const unsigned char *) value;
        size_t n = 0;
        while (n < value_len && p[n] != '\0')
            n++;
        value_len = n;
    }
    if (!musicpack_utf8_valid((const unsigned char *) value, value_len))
        return MUSICPACK_ERR_INVALID;
    return tag_set_add_impl(s, key, value, value_len, 0, 0);
}

musicpack_status
musicpack_tag_set_add_binary(musicpack_tag_set *s, const char *key,
                             const unsigned char *data, size_t len)
{
    return tag_set_add_impl(s, key, 0, 0, data, len);
}

const musicpack_tag *
musicpack_tag_set_get(const musicpack_tag_set *s, const char *key)
{
    size_t i;

    if (s == 0 || key == 0)
        return 0;
    for (i = 0; i < s->count; i++)
        if (key_eq(s->items[i].key, key))
            return &s->items[i];
    return 0;
}

size_t
musicpack_tag_set_get_all(const musicpack_tag_set *s, const char *key,
                          const musicpack_tag **out, size_t cap)
{
    size_t i, n = 0;

    if (s == 0 || key == 0 || out == 0)
        return 0;
    for (i = 0; i < s->count && n < cap; i++)
        if (key_eq(s->items[i].key, key))
            out[n++] = &s->items[i];
    return n;
}
