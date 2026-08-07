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
/// \file path.c
/// Canonical `.mpack` path rules and containment resolution.

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
# include <windows.h>
#else
# include <stdlib.h> /* realpath */
#endif

#include <musicpack/path.h>

static int
is_ctrl(char c)
{
    return (unsigned char) c < 0x20 || (unsigned char) c == 0x7f;
}

musicpack_status
musicpack_path_validate(const char *rel)
{
    size_t len, i, seg_start = 0;

    if (rel == 0)
        return MUSICPACK_ERR_PATH;
    len = strlen(rel);
    if (len == 0 || len > MUSICPACK_PATH_MAX)
        return MUSICPACK_ERR_PATH;

    for (i = 0; i < len; i++) {
        char c = rel[i];
        if (c == '\\' || c == ':' || is_ctrl(c))
            return MUSICPACK_ERR_PATH;  /* backslash, drive/URL colon, control */
        if (c == '/') {
            size_t seg_len = i - seg_start;
            if (i == 0)                 /* absolute path */
                return MUSICPACK_ERR_PATH;
            if (seg_len == 0)           /* empty segment */
                return MUSICPACK_ERR_PATH;
            if (seg_len == 1 && rel[seg_start] == '.')
                return MUSICPACK_ERR_PATH;  /* '.' segment */
            if (seg_len == 2 && rel[seg_start] == '.' && rel[seg_start + 1] == '.')
                return MUSICPACK_ERR_PATH;  /* '..' segment */
            seg_start = i + 1;
        }
    }
    /* final segment */
    if (seg_start == len)
        return MUSICPACK_ERR_PATH;          /* trailing '/' */
    if (len - seg_start == 1 && rel[seg_start] == '.')
        return MUSICPACK_ERR_PATH;
    if (len - seg_start == 2 && rel[seg_start] == '.' && rel[seg_start + 1] == '.')
        return MUSICPACK_ERR_PATH;

    return MUSICPACK_OK;
}

static int
is_within(const char *base, const char *candidate)
{
    size_t blen = strlen(base);
    if (strncmp(base, candidate, blen) != 0)
        return 0;
    if (candidate[blen] == '\0' || candidate[blen] == '/')
        return 1;
    return 0;
}

musicpack_status
musicpack_path_resolve(const char *root, const char *rel, char *out, size_t cap)
{
    char joined[MUSICPACK_PATH_MAX + 2];

    if (root == 0 || out == 0 || cap == 0)
        return MUSICPACK_ERR_INVALID;
    if (musicpack_path_validate(rel) != MUSICPACK_OK)
        return MUSICPACK_ERR_PATH;
    if (snprintf(joined, sizeof joined, "%s/%s", root, rel) >= (int) sizeof joined)
        return MUSICPACK_ERR_PATH;
    if (strlen(joined) >= cap)
        return MUSICPACK_ERR_PATH;

#if defined(_WIN32)
    {
        char root_abs[MUSICPACK_PATH_MAX + 2], joined_abs[MUSICPACK_PATH_MAX + 2];
        DWORD n1, n2;
        n1 = GetFullPathNameA(root, (DWORD) sizeof root_abs, root_abs, NULL);
        n2 = GetFullPathNameA(joined, (DWORD) sizeof joined_abs, joined_abs, NULL);
        if (n1 == 0 || n2 == 0)
            return MUSICPACK_ERR_PATH;
        if (!is_within(root_abs, joined_abs))
            return MUSICPACK_ERR_PATH;
        if (strlen(joined_abs) >= cap)
            return MUSICPACK_ERR_PATH;
        memcpy(out, joined_abs, strlen(joined_abs) + 1);
        return MUSICPACK_OK;
    }
#else
    {
        char root_real[MUSICPACK_PATH_MAX + 2], joined_real[MUSICPACK_PATH_MAX + 2];
        if (realpath(root, root_real) == 0)
            return MUSICPACK_ERR_PATH;
        if (realpath(joined, joined_real) == 0) {
            /* target does not exist yet: nothing to escape to; fall back to
               the lexical join (already free of '..'). */
            memcpy(out, joined, strlen(joined) + 1);
            return MUSICPACK_OK;
        }
        if (!is_within(root_real, joined_real))
            return MUSICPACK_ERR_PATH;
        if (strlen(joined_real) >= cap)
            return MUSICPACK_ERR_PATH;
        memcpy(out, joined_real, strlen(joined_real) + 1);
        return MUSICPACK_OK;
    }
#endif
}
