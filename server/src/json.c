/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see json.h)
*/
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MPJ_OBJ,
    MPJ_ARR,
    MPJ_STR,
    MPJ_INT,
    MPJ_DBL,
};

struct mp_json {
    int type;
    char *key;
    char *str;
    long long i64;
    double dbl;
    mp_json *first, *last, *next;
};

mp_json *
mp_json_obj(void)
{
    mp_json *j = (mp_json *) calloc(1, sizeof *j);
    j->type = MPJ_OBJ;
    return j;
}

mp_json *
mp_json_arr(void)
{
    mp_json *j = (mp_json *) calloc(1, sizeof *j);
    j->type = MPJ_ARR;
    return j;
}

static mp_json *
leaf(int type)
{
    mp_json *j = (mp_json *) calloc(1, sizeof *j);
    j->type = type;
    return j;
}

void
mp_json_free(mp_json *j)
{
    mp_json *c, *n;
    if (j == 0)
        return;
    free(j->key);
    free(j->str);
    for (c = j->first; c != 0; c = n) {
        n = c->next;
        mp_json_free(c);
    }
    free(j);
}

static void
attach(mp_json *parent, mp_json *child)
{
    if (parent->last == 0) {
        parent->first = parent->last = child;
    } else {
        parent->last->next = child;
        parent->last = child;
    }
}

void
mp_json_add(mp_json *parent, const char *key, mp_json *child)
{
    if (parent == 0 || child == 0)
        return;
    if (parent->type == MPJ_OBJ && key != 0)
        child->key = strdup(key);
    attach(parent, child);
}

void
mp_json_str(mp_json *o, const char *key, const char *value)
{
    mp_json *j = leaf(MPJ_STR);
    j->str = strdup(value != 0 ? value : "");
    mp_json_add(o, key, j);
}

void
mp_json_str_opt(mp_json *o, const char *key, const char *value)
{
    if (value == 0 || *value == '\0')
        return;
    mp_json_str(o, key, value);
}

void
mp_json_int(mp_json *o, const char *key, long long value)
{
    mp_json *j = leaf(MPJ_INT);
    j->i64 = value;
    mp_json_add(o, key, j);
}

void
mp_json_dbl(mp_json *o, const char *key, double value)
{
    mp_json *j = leaf(MPJ_DBL);
    j->dbl = value;
    mp_json_add(o, key, j);
}

mp_json *
mp_json_strnode(const char *value)
{
    mp_json *j = leaf(MPJ_STR);
    j->str = strdup(value != 0 ? value : "");
    return j;
}

mp_json *
mp_json_intnode(long long value)
{
    mp_json *j = leaf(MPJ_INT);
    j->i64 = value;
    return j;
}

/* ---- rendering --------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} sbuf;

static void
sbuf_reserve(sbuf *s, size_t extra)
{
    if (s->len + extra + 1 > s->cap) {
        size_t nc = s->cap == 0 ? 256 : s->cap * 2;
        while (nc < s->len + extra + 1)
            nc *= 2;
        s->buf = (char *) realloc(s->buf, nc);
        s->cap = nc;
    }
}

static void
sbuf_raw(sbuf *s, const char *txt, size_t n)
{
    sbuf_reserve(s, n);
    memcpy(s->buf + s->len, txt, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

static void
sbuf_cstr(sbuf *s, const char *txt)
{
    sbuf_raw(s, txt, strlen(txt));
}

static void
sbuf_char(sbuf *s, char c)
{
    sbuf_reserve(s, 1);
    s->buf[s->len++] = c;
    s->buf[s->len] = '\0';
}

static void
sbuf_esc(sbuf *s, const char *v)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p = (const unsigned char *) v;
    sbuf_char(s, '"');
    for (; *p != '\0'; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"':  sbuf_raw(s, "\\\"", 2); break;
        case '\\': sbuf_raw(s, "\\\\", 2); break;
        case '\b': sbuf_raw(s, "\\b", 2); break;
        case '\f': sbuf_raw(s, "\\f", 2); break;
        case '\n': sbuf_raw(s, "\\n", 2); break;
        case '\r': sbuf_raw(s, "\\r", 2); break;
        case '\t': sbuf_raw(s, "\\t", 2); break;
        default:
            if (c < 0x20) {
                char esc[7] = { '\\', 'u', '0', '0', 0, 0, 0 };
                esc[4] = hex[c >> 4];
                esc[5] = hex[c & 0x0f];
                sbuf_raw(s, esc, 6);
            } else {
                sbuf_char(s, (char) c);
            }
        }
    }
    sbuf_char(s, '"');
}

static void
render(mp_json *j, sbuf *s)
{
    mp_json *c;
    int first = 1;

    switch (j->type) {
    case MPJ_STR:
        sbuf_esc(s, j->str);
        break;
    case MPJ_INT:
        {
            char tmp[32];
            snprintf(tmp, sizeof tmp, "%lld", j->i64);
            sbuf_cstr(s, tmp);
        }
        break;
    case MPJ_DBL:
        {
            char tmp[64];
            snprintf(tmp, sizeof tmp, "%.10g", j->dbl);
            sbuf_cstr(s, tmp);
        }
        break;
    case MPJ_OBJ:
        sbuf_char(s, '{');
        for (c = j->first; c != 0; c = c->next) {
            if (!first)
                sbuf_char(s, ',');
            first = 0;
            sbuf_esc(s, c->key != 0 ? c->key : "");
            sbuf_char(s, ':');
            render(c, s);
        }
        sbuf_char(s, '}');
        break;
    case MPJ_ARR:
        sbuf_char(s, '[');
        for (c = j->first; c != 0; c = c->next) {
            if (!first)
                sbuf_char(s, ',');
            first = 0;
            render(c, s);
        }
        sbuf_char(s, ']');
        break;
    }
}

char *
mp_json_render(mp_json *j)
{
    sbuf s;
    if (j == 0)
        return strdup("null");
    memset(&s, 0, sizeof s);
    render(j, &s);
    sbuf_reserve(&s, 1);
    if (s.buf == 0)
        return strdup("null");
    return s.buf;
}

char *
mp_json_error(const char *code, const char *message)
{
    mp_json *root = mp_json_obj();
    mp_json *err = mp_json_obj();
    char *out;

    mp_json_add(root, "error", err);
    mp_json_str(err, "code", code);
    mp_json_str(err, "message", message);
    out = mp_json_render(root);
    mp_json_free(root);
    return out;
}
