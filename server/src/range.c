/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see range.h)
*/
#include "range.h"

#include <ctype.h>
#include <string.h>

/* Parses a decimal number, rejecting leading '+', empty, or overflow.
   Returns 1 on success and sets *out / *end (points past the digits). */
static int
parse_num(const char *s, const char **end, long long *out)
{
    long long v = 0;
    const char *p = s;

    if (*p < '0' || *p > '9')
        return 0;
    while (*p >= '0' && *p <= '9') {
        int d = *p - '0';
        if (v > (9223372036854775807LL - d) / 10)
            return 0; /* overflow */
        v = v * 10 + d;
        p++;
    }
    *end = p;
    *out = v;
    return 1;
}

mp_range_result
mp_range_parse(const char *header, long long size, mp_range *out)
{
    const char *p;
    long long first, last;
    int has_first, has_last;

    if (header == 0 || size < 0 || out == 0)
        return MP_RANGE_INVALID;
    if (strncmp(header, "bytes=", 6) != 0)
        return MP_RANGE_INVALID;
    p = header + 6;

    /* reject multiple ranges */
    if (strchr(p, ',') != 0)
        return MP_RANGE_INVALID;

    if (*p == '-') {
        /* suffix range: bytes=-N (last N bytes) */
        p++;
        if (!parse_num(p, &p, &first) || *p != '\0')
            return MP_RANGE_INVALID;
        if (first == 0)
            return MP_RANGE_INVALID; /* bytes=-0 is malformed per RFC */
        if (size == 0)
            return MP_RANGE_UNSATISFIABLE;
        if (first > size)
            first = size;
        out->start = size - first;
        out->length = first;
        return MP_RANGE_OK;
    }

    has_first = parse_num(p, &p, &first);
    if (!has_first)
        return MP_RANGE_INVALID;
    if (*p != '-')
        return MP_RANGE_INVALID;
    p++;
    if (*p == '\0') {
        has_last = 0;
        last = 0;
    } else {
        has_last = 1;
        if (!parse_num(p, &p, &last) || *p != '\0')
            return MP_RANGE_INVALID;
    }
    if (first >= size)
        return MP_RANGE_UNSATISFIABLE;
    if (has_last && last < first)
        return MP_RANGE_INVALID; /* bytes=5-2 */
    if (has_last && last >= size)
        last = size - 1;
    if (!has_last)
        last = size - 1;
    out->start = first;
    out->length = last - first + 1;
    return MP_RANGE_OK;
}
