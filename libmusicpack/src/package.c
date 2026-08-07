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
/// \file package.c
/// Directory-form `.mpack` package handle.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
# include <windows.h>
#else
# include <dirent.h>
# include <sys/stat.h>
# include <unistd.h>
#endif

#include "internal.h"
#include <musicpack/checksum.h>
#include <musicpack/path.h>

#define MANIFEST_NAME "manifest.json"
#define MANIFEST_MAX  (16u * 1024u * 1024u)

struct musicpack_package {
    char *root;             /* absolute package root */
    musicpack_manifest *manifest;
    cJSON *original;        /* original manifest tree (unknown-field save) */
};

/* ------------------------------------------------------------------ */
/* manifest file I/O                                                   */
/* ------------------------------------------------------------------ */

static char *
read_file(const char *path, size_t max, musicpack_status *status)
{
    FILE *f;
    long len;
    char *buf;

    f = fopen(path, "rb");
    if (f == 0) {
        *status = MUSICPACK_ERR_IO;
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); *status = MUSICPACK_ERR_IO; return 0; }
    len = ftell(f);
    if (len < 0 || (size_t) len > max) { fclose(f); *status = MUSICPACK_ERR_IO; return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); *status = MUSICPACK_ERR_IO; return 0; }
    buf = (char *) malloc((size_t) len + 1);
    if (buf == 0) { fclose(f); *status = MUSICPACK_ERR_NOMEM; return 0; }
    if (len > 0 && fread(buf, 1, (size_t) len, f) != (size_t) len) {
        free(buf); fclose(f); *status = MUSICPACK_ERR_IO; return 0;
    }
    fclose(f);
    buf[len] = '\0';
    *status = MUSICPACK_OK;
    return buf;
}

static musicpack_status
write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    size_t len = strlen(text);
    if (f == 0)
        return MUSICPACK_ERR_IO;
    if (len > 0 && fwrite(text, 1, len, f) != len) {
        fclose(f);
        return MUSICPACK_ERR_IO;
    }
    if (fclose(f) != 0)
        return MUSICPACK_ERR_IO;
    return MUSICPACK_OK;
}

/* ------------------------------------------------------------------ */
/* open / close                                                        */
/* ------------------------------------------------------------------ */

musicpack_package *
musicpack_package_open_dir(const char *dir, musicpack_status *status)
{
    musicpack_package *pkg;
    musicpack_status local = MUSICPACK_OK;
    char *json, manifest_path[MUSICPACK_PATH_MAX + 2];
    cJSON *root;

    if (status == 0)
        status = &local;
    *status = MUSICPACK_OK;
    if (dir == 0) {
        *status = MUSICPACK_ERR_INVALID;
        return 0;
    }
    if (snprintf(manifest_path, sizeof manifest_path, "%s/%s", dir, MANIFEST_NAME)
            >= (int) sizeof manifest_path) {
        *status = MUSICPACK_ERR_PATH;
        return 0;
    }

    json = read_file(manifest_path, MANIFEST_MAX, status);
    if (json == 0)
        return 0;

    root = cJSON_ParseWithLength(json, strlen(json));
    if (root == 0) {
        free(json);
        *status = MUSICPACK_ERR_JSON;
        return 0;
    }

    pkg = (musicpack_package *) calloc(1, sizeof *pkg);
    if (pkg == 0) {
        free(json);
        cJSON_Delete(root);
        *status = MUSICPACK_ERR_NOMEM;
        return 0;
    }
    pkg->manifest = (musicpack_manifest *) calloc(1, sizeof *pkg->manifest);
    if (pkg->manifest == 0) {
        free(pkg);
        free(json);
        cJSON_Delete(root);
        *status = MUSICPACK_ERR_NOMEM;
        return 0;
    }
    *status = musicpack_manifest_parse_tree(root, pkg->manifest);
    if (*status != MUSICPACK_OK) {
        musicpack_manifest_free(pkg->manifest);
        free(pkg);
        free(json);
        cJSON_Delete(root);
        return 0;
    }
    pkg->original = root;
    free(json);

    pkg->root = strdup(dir);
    if (pkg->root == 0) {
        musicpack_manifest_free(pkg->manifest);
        cJSON_Delete(pkg->original);
        free(pkg);
        *status = MUSICPACK_ERR_NOMEM;
        return 0;
    }
    return pkg;
}

void
musicpack_package_close(musicpack_package *pkg)
{
    if (pkg == 0)
        return;
    musicpack_manifest_free(pkg->manifest);
    cJSON_Delete(pkg->original);
    free(pkg->root);
    free(pkg);
}

const musicpack_manifest *
musicpack_package_manifest(const musicpack_package *pkg)
{
    return pkg != 0 ? pkg->manifest : 0;
}

musicpack_status
musicpack_package_resolve_path(const musicpack_package *pkg, const char *rel,
                               char *out, size_t cap)
{
    if (pkg == 0)
        return MUSICPACK_ERR_INVALID;
    return musicpack_path_resolve(pkg->root, rel, out, cap);
}

/* ------------------------------------------------------------------ */
/* verify                                                              */
/* ------------------------------------------------------------------ */

static void
report(musicpack_report *rep, musicpack_report_fn fn, void *ctx,
       const char *message, int is_error)
{
    if (is_error)
        rep->errors++;
    else
        rep->warnings++;
    if (fn != 0)
        fn(ctx, message, is_error);
}

static int
file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == 0)
        return 0;
    fclose(f);
    return 1;
}

static void
verify_assets(const musicpack_package *pkg, const musicpack_asset *assets,
              size_t count, const char *kind, musicpack_report *rep,
              musicpack_report_fn fn, void *ctx, int *failed)
{
    size_t i;

    for (i = 0; i < count; i++) {
        const musicpack_asset *a = &assets[i];
        char abs[MUSICPACK_PATH_MAX + 2];
        char buf[512];

        if (musicpack_package_resolve_path(pkg, a->path, abs, sizeof abs) != MUSICPACK_OK) {
            snprintf(buf, sizeof buf, "%s: unsafe path '%s'", kind, a->path);
            report(rep, fn, ctx, buf, 1);
            *failed = 1;
            continue;
        }
        if (!file_exists(abs)) {
            snprintf(buf, sizeof buf, "%s: missing file '%s'", kind, a->path);
            report(rep, fn, ctx, buf, 1);
            *failed = 1;
            continue;
        }
        if (a->sha256 != 0) {
            char hex[MUSICPACK_SHA256_HEX_SIZE];
            if (musicpack_sha256_file(abs, hex, sizeof hex) != MUSICPACK_OK) {
                snprintf(buf, sizeof buf, "%s: cannot hash '%s'", kind, a->path);
                report(rep, fn, ctx, buf, 1);
                *failed = 1;
            } else if (!musicpack_sha256_eq(hex, a->sha256)) {
                snprintf(buf, sizeof buf, "%s: checksum mismatch '%s'", kind, a->path);
                report(rep, fn, ctx, buf, 1);
                *failed = 1;
            }
        }
    }
}

#if !defined(_WIN32)
static void
walk_dir(const char *abs_base, const char *rel_base, char ***files, size_t *count,
         size_t *cap)
{
    DIR *d;
    struct dirent *e;
    char abspath[MUSICPACK_PATH_MAX + 2];

    d = opendir(abs_base);
    if (d == 0)
        return;
    while ((e = readdir(d)) != 0) {
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (snprintf(abspath, sizeof abspath, "%s/%s", abs_base, e->d_name)
                >= (int) sizeof abspath)
            continue;
        if (lstat(abspath, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            char relnext[MUSICPACK_PATH_MAX + 2];
            if (rel_base[0] == '\0')
                snprintf(relnext, sizeof relnext, "%s", e->d_name);
            else
                snprintf(relnext, sizeof relnext, "%s/%s", rel_base, e->d_name);
            walk_dir(abspath, relnext, files, count, cap);
        } else if (S_ISREG(st.st_mode)) {
            char *rel;
            if (*count >= *cap) {
                size_t newcap = *cap == 0 ? 64 : *cap * 2;
                char **nf = (char **) realloc(*files, newcap * sizeof *nf);
                if (nf == 0)
                    break;
                *files = nf;
                *cap = newcap;
            }
            if (rel_base[0] == '\0')
                rel = strdup(e->d_name);
            else {
                size_t n = snprintf(0, 0, "%s/%s", rel_base, e->d_name);
                rel = (char *) malloc(n + 1);
                if (rel != 0)
                    snprintf(rel, n + 1, "%s/%s", rel_base, e->d_name);
            }
            if (rel != 0)
                (*files)[(*count)++] = rel;
        }
    }
    closedir(d);
}
#endif

static void
verify_extra_files(const musicpack_package *pkg, musicpack_report *rep,
                   musicpack_report_fn fn, void *ctx)
{
#if !defined(_WIN32)
    char **files = 0;
    size_t count = 0, cap = 0, i;
    const musicpack_manifest *m = pkg->manifest;

    walk_dir(pkg->root, "", &files, &count, &cap);
    for (i = 0; i < count; i++) {
        int referenced = strcmp(files[i], MANIFEST_NAME) == 0;
        size_t d, t, a;
        if (!referenced)
            for (d = 0; d < m->disc_count && !referenced; d++)
                for (t = 0; t < m->discs[d].track_count && !referenced; t++)
                    if (strcmp(m->discs[d].tracks[t].audio.path, files[i]) == 0)
                        referenced = 1;
        if (!referenced)
            for (a = 0; a < m->artwork_count && !referenced; a++)
                if (strcmp(m->artwork[a].asset.path, files[i]) == 0)
                    referenced = 1;
        if (!referenced)
            for (a = 0; a < m->booklet_count && !referenced; a++)
                if (strcmp(m->booklet[a].path, files[i]) == 0)
                    referenced = 1;
        if (!referenced)
            for (a = 0; a < m->lyrics_count && !referenced; a++)
                if (strcmp(m->lyrics[a].path, files[i]) == 0)
                    referenced = 1;
        if (!referenced)
            for (a = 0; a < m->extras_count && !referenced; a++)
                if (strcmp(m->extras[a].path, files[i]) == 0)
                    referenced = 1;

        if (!referenced) {
            char buf[512];
            snprintf(buf, sizeof buf, "unreferenced file '%s'", files[i]);
            report(rep, fn, ctx, buf, 0);
        }
        free(files[i]);
    }
    free(files);
#endif
}

musicpack_status
musicpack_package_verify(const musicpack_package *pkg, musicpack_report *rep,
                         musicpack_report_fn fn, void *ctx)
{
    const musicpack_manifest *m;
    musicpack_report local = { 0, 0 };
    int failed = 0;
    size_t d, t;

    if (pkg == 0)
        return MUSICPACK_ERR_INVALID;
    if (rep == 0)
        rep = &local;

    m = pkg->manifest;
    for (d = 0; d < m->disc_count; d++) {
        for (t = 0; t < m->discs[d].track_count; t++) {
            musicpack_track *tr = &m->discs[d].tracks[t];
            verify_assets(pkg, &tr->audio, 1, "track", rep, fn, ctx, &failed);
        }
    }
    {
        /* artwork carries a role; verify_assets works on musicpack_asset* */
        musicpack_asset *tmp = 0;
        size_t n = 0, i;
        /* flatten artwork into a temporary asset list for hashing */
        if (m->artwork_count > 0) {
            tmp = (musicpack_asset *) malloc(m->artwork_count * sizeof *tmp);
            if (tmp != 0) {
                for (i = 0; i < m->artwork_count; i++)
                    tmp[i] = m->artwork[i].asset;
                n = m->artwork_count;
            }
        }
        verify_assets(pkg, tmp, n, "artwork", rep, fn, ctx, &failed);
        free(tmp);
    }
    verify_assets(pkg, m->booklet, m->booklet_count, "booklet", rep, fn, ctx, &failed);
    verify_assets(pkg, m->lyrics, m->lyrics_count, "lyrics", rep, fn, ctx, &failed);
    verify_assets(pkg, m->extras, m->extras_count, "extras", rep, fn, ctx, &failed);

    verify_extra_files(pkg, rep, fn, ctx);

    return failed ? MUSICPACK_ERR_CHECKSUM : MUSICPACK_OK;
}

musicpack_status
musicpack_package_save_manifest(const musicpack_package *pkg)
{
    char *json = 0;
    char path[MUSICPACK_PATH_MAX + 2];
    musicpack_status s;

    if (pkg == 0)
        return MUSICPACK_ERR_INVALID;
    s = musicpack_manifest_write_with_original(pkg->manifest, pkg->original, &json);
    if (s != MUSICPACK_OK)
        return s;
    if (snprintf(path, sizeof path, "%s/%s", pkg->root, MANIFEST_NAME)
            >= (int) sizeof path) {
        free(json);
        return MUSICPACK_ERR_PATH;
    }
    s = write_file(path, json);
    free(json);
    return s;
}
