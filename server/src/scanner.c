/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see scanner.h)
*/
#include "scanner.h"
#include "codec.h"
#include "identity.h"
#include "log.h"
#include "mime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <musicpack/checksum.h>

#ifdef _WIN32
# include <sys/stat.h>
#else
# include <dirent.h>
# include <sys/stat.h>
#endif

#define MANIFEST_MAX (16u * 1024u * 1024u)

static int
ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}

static char *
read_file_bounded(const char *path, size_t max)
{
    FILE *f = fopen(path, "rb");
    long len;
    char *buf;

    if (f == 0)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    len = ftell(f);
    if (len < 0 || (unsigned long) len > max) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    buf = (char *) malloc((size_t) len + 1);
    if (buf == 0) { fclose(f); return 0; }
    if (len > 0 && fread(buf, 1, (size_t) len, f) != (size_t) len) {
        free(buf); fclose(f); return 0;
    }
    fclose(f);
    buf[len] = '\0';
    return buf;
}

/* Resolves each referenced object and checks existence (lightweight scan
   policy: no full hashing). Returns the number of missing objects. */
static int
count_missing_objects(const musicpack_package *pkg, const musicpack_manifest *m)
{
    int missing = 0;
    size_t d, t, i;
    char out[MUSICPACK_PATH_MAX + 2];
    for (d = 0; d < m->disc_count; d++)
        for (t = 0; t < m->discs[d].track_count; t++)
            if (musicpack_package_resolve_path(
                    pkg, m->discs[d].tracks[t].audio.path, out, sizeof out)
                != MUSICPACK_OK)
                missing++;
    for (i = 0; i < m->artwork_count; i++)
        if (musicpack_package_resolve_path(pkg, m->artwork[i].asset.path,
                                           out, sizeof out) != MUSICPACK_OK)
            missing++;
    for (i = 0; i < m->booklet_count; i++)
        if (musicpack_package_resolve_path(pkg, m->booklet[i].path, out,
                                           sizeof out) != MUSICPACK_OK)
            missing++;
    for (i = 0; i < m->lyrics_count; i++)
        if (musicpack_package_resolve_path(pkg, m->lyrics[i].path, out,
                                           sizeof out) != MUSICPACK_OK)
            missing++;
    for (i = 0; i < m->extras_count; i++)
        if (musicpack_package_resolve_path(pkg, m->extras[i].path, out,
                                           sizeof out) != MUSICPACK_OK)
            missing++;
    return missing;
}

/* Determines status + verify_status + last_error for a parsed package,
   according to the scan policy (lightweight vs full verification). */
static void
determine_status(const musicpack_package *pkg, const musicpack_manifest *m,
                 int verify, const char **status, const char **verify_status,
                 char *errbuf, size_t errcap)
{
    *status = "valid";
    *verify_status = "unverified";
    if (!verify) {
        int missing = count_missing_objects(pkg, m);
        if (missing > 0) {
            *status = "warning";
            snprintf(errbuf, errcap, "%d referenced object(s) missing", missing);
        }
    } else {
        musicpack_report rep = { 0, 0 };
        if (musicpack_package_verify(pkg, &rep, 0, 0) != MUSICPACK_OK) {
            *status = "checksum-failed";
            *verify_status = "checksum-failed";
            snprintf(errbuf, errcap, "integrity verification failed "
                     "(%zu errors, %zu warnings)", rep.errors, rep.warnings);
        } else if (rep.warnings > 0) {
            *status = "warning";
            *verify_status = "warning";
            snprintf(errbuf, errcap, "%zu warning(s)", rep.warnings);
        } else {
            *status = "valid";
            *verify_status = "valid";
        }
    }
}

/* Collects per-track codec info + resolved absolute paths in manifest order. */
static mp_track_ingest *
collect_track_ingest(const musicpack_package *pkg, const musicpack_manifest *m,
                     size_t *count_out)
{
    size_t n = 0, d, t, i = 0;
    mp_track_ingest *out;
    for (d = 0; d < m->disc_count; d++)
        n += m->discs[d].track_count;
    if (n == 0) {
        *count_out = 0;
        return 0;
    }
    out = (mp_track_ingest *) calloc(n, sizeof *out);
    if (out == 0) {
        *count_out = 0;
        return 0;
    }
    for (d = 0; d < m->disc_count; d++)
        for (t = 0; t < m->discs[d].track_count; t++) {
            if (musicpack_package_track_path(pkg, d, t, out[i].abs_path,
                                             sizeof out[i].abs_path)
                == MUSICPACK_OK)
                mp_codec_probe(out[i].abs_path,
                               m->discs[d].tracks[t].audio.path,
                               &out[i].codec);
            i++;
        }
    *count_out = n;
    return out;
}

/* Full ingest: upsert group/release + replace content + package row, inside
   one transaction. Returns 0 on success. */
static int
ingest_valid(mp_library *lib, const char *dir, const musicpack_package *pkg,
             const char *manifest_sha, const char *last_scan, int verify,
             mp_scan_result *res)
{
    const musicpack_manifest *m = musicpack_package_manifest(pkg);
    char fingerprint[MP_ID_KEY_MAX];
    char group_key[MP_ID_KEY_MAX];
    char release_key[MP_ID_KEY_MAX];
    char errbuf[160];
    long long group_id, release_id;
    mp_track_ingest *codecs = 0;
    size_t codec_count = 0;
    const char *status, *verify_status, *last_error = 0;
    mp_package_row row;
    int have_row = mp_library_package_by_path(lib, dir, &row);

    if (mp_identity_package_fingerprint(m, fingerprint, sizeof fingerprint)
        != MUSICPACK_OK ||
        mp_identity_group_key(m, group_key, sizeof group_key) != MUSICPACK_OK ||
        mp_identity_release_key(m, release_key, sizeof release_key)
            != MUSICPACK_OK)
        return -1;

    if (mp_library_begin(lib) != 0)
        return -1;
    group_id = mp_library_upsert_group(lib, m, group_key);
    release_id = mp_library_upsert_release(lib, m, group_id, release_key);
    if (group_id < 0 || release_id < 0) {
        mp_library_rollback(lib);
        return -1;
    }
    codecs = collect_track_ingest(pkg, m, &codec_count);
    if (mp_library_replace_release_content(lib, release_id, m, dir, codecs,
                                           codec_count) != 0) {
        free(codecs);
        mp_library_rollback(lib);
        return -1;
    }
    free(codecs);

    determine_status(pkg, m, verify, &status, &verify_status,
                     errbuf, sizeof errbuf);
    if (strcmp(status, "valid") != 0)
        last_error = errbuf;

    if (have_row) {
        if (mp_library_package_update(lib, row.id, release_id, dir,
                                      fingerprint, manifest_sha, status,
                                      verify_status, last_scan,
                                      last_error) != 0) {
            mp_library_rollback(lib);
            return -1;
        }
        res->updated++;
    } else {
        if (mp_library_package_insert(lib, dir, release_id, fingerprint,
                                      manifest_sha, status, verify_status,
                                      last_scan, last_error) < 0) {
            mp_library_rollback(lib);
            return -1;
        }
        res->added++;
    }
    if (mp_library_commit(lib) != 0) {
        mp_library_rollback(lib);
        return -1;
    }
    return 0;
}

/* A package already known by content fingerprint is a move: update the
   existing row's path (identity and release stay). Returns 1 when handled. */
static int
handle_move(mp_library *lib, const char *dir, const musicpack_package *pkg,
            const char *manifest_sha, const char *last_scan, int verify,
            mp_scan_result *res)
{
    const musicpack_manifest *m = musicpack_package_manifest(pkg);
    char fingerprint[MP_ID_KEY_MAX];
    char errbuf[160];
    mp_package_row row;
    const char *status, *verify_status, *last_error = 0;

    if (mp_identity_package_fingerprint(m, fingerprint, sizeof fingerprint)
        != MUSICPACK_OK)
        return 0;
    if (!mp_library_package_by_fingerprint(lib, fingerprint, &row))
        return 0;
    if (strcmp(row.path, dir) == 0)
        return 0;
    determine_status(pkg, m, verify, &status, &verify_status,
                     errbuf, sizeof errbuf);
    if (strcmp(status, "valid") != 0)
        last_error = errbuf;
    if (mp_library_begin(lib) != 0)
        return 0;
    if (mp_library_package_update(lib, row.id, row.release_id, dir,
                                  fingerprint, manifest_sha, status,
                                  verify_status, last_scan, last_error) == 0) {
        mp_library_commit(lib);
        res->moved++;
        return 1;
    }
    mp_library_rollback(lib);
    return 0;
}

static void
record_invalid(mp_library *lib, const char *dir, const char *manifest_sha,
               const char *last_scan, const char *reason, mp_scan_result *res)
{
    mp_package_row row;
    int have_row = mp_library_package_by_path(lib, dir, &row);

    if (mp_library_begin(lib) != 0)
        return;
    if (have_row) {
        if (mp_library_package_update(lib, row.id, -1, dir, "", manifest_sha,
                                      "invalid", "unverified", last_scan,
                                      reason) == 0)
            res->updated++;
    } else {
        if (mp_library_package_insert(lib, dir, -1, "", manifest_sha,
                                      "invalid", "unverified", last_scan,
                                      reason) >= 0)
            res->invalid++;
    }
    mp_library_commit(lib);
}

static void
process_package(mp_library *lib, const char *dir, const char *last_scan,
                int verify, mp_scan_result *res)
{
    char mpath[MUSICPACK_PATH_MAX + 2];
    char *json;
    char manifest_sha[MUSICPACK_SHA256_HEX_SIZE];
    mp_package_row row;
    musicpack_package *pkg;

    snprintf(mpath, sizeof mpath, "%s/manifest.json", dir);
    json = read_file_bounded(mpath, MANIFEST_MAX);
    if (json == 0) {
        MP_LOGW("invalid package %s: manifest.json unreadable or too large",
                dir);
        record_invalid(lib, dir, "", last_scan, "manifest.json unreadable",
                       res);
        res->total++;
        return;
    }
    mp_identity_manifest_hash(json, strlen(json), manifest_sha,
                              sizeof manifest_sha);

    /* fast path: unchanged package (same path, same manifest hash) */
    if (mp_library_package_by_path(lib, dir, &row) &&
        strcmp(row.manifest_sha256, manifest_sha) == 0) {
        free(json);
        mp_library_begin(lib);
        mp_library_package_update(lib, row.id, row.release_id, dir,
                                  row.fingerprint, row.manifest_sha256,
                                  row.status, row.verify_status, last_scan, 0);
        mp_library_commit(lib);
        res->total++;
        return;
    }

    pkg = musicpack_package_open_dir(dir, 0);
    if (pkg == 0) {
        MP_LOGW("invalid package %s: manifest parse/validation failed", dir);
        record_invalid(lib, dir, manifest_sha, last_scan,
                       "manifest parse/validation failed", res);
        free(json);
        res->total++;
        return;
    }
    if (!mp_library_package_by_path(lib, dir, &row) &&
        handle_move(lib, dir, pkg, manifest_sha, last_scan, verify, res)) {
        musicpack_package_close(pkg);
        free(json);
        res->total++;
        return;
    }
    if (ingest_valid(lib, dir, pkg, manifest_sha, last_scan, verify, res) != 0)
        MP_LOGE("scan: failed to ingest package %s", dir);
    musicpack_package_close(pkg);
    free(json);
    res->total++;
}

static void
walk(mp_library *lib, const char *abs, const char *last_scan, int verify,
     mp_scan_result *res)
{
    DIR *d = opendir(abs);
    struct dirent *e;

    if (d == 0) {
        MP_LOGW("scan: cannot open directory %s", abs);
        return;
    }
    while ((e = readdir(d)) != 0) {
        char next[MUSICPACK_PATH_MAX + 2];
        int is_dir = 0;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        snprintf(next, sizeof next, "%s/%s", abs, e->d_name);
#ifdef _WIN32
        {
            struct _stat st;
            if (_stat(next, &st) == 0 && (st.st_mode & _S_IFDIR))
                is_dir = 1;
        }
#else
        {
            struct stat st;
            if (lstat(next, &st) != 0)
                continue;
            if (S_ISLNK(st.st_mode)) {
                MP_LOGD("skipping symlink %s", next);
                continue;
            }
            if (S_ISDIR(st.st_mode))
                is_dir = 1;
        }
#endif
        if (!is_dir)
            continue;
        if (ends_with(e->d_name, ".mpack"))
            process_package(lib, next, last_scan, verify, res);
        else
            walk(lib, next, last_scan, verify, res);
    }
    closedir(d);
}

musicpack_status
mp_scan_library(mp_library *lib, const char *root, int verify,
                mp_scan_result *res)
{
    static unsigned counter;
    char last_scan[64];

    if (res != 0)
        memset(res, 0, sizeof *res);
    snprintf(last_scan, sizeof last_scan, "s%ld.%u", (long) time(0),
             counter++);
    MP_LOGI("scan start (root=%s, verify=%s)", root, verify ? "yes" : "no");
    walk(lib, root, last_scan, verify, res);
    if (res != 0) {
        int removed = mp_library_package_sweep(lib, last_scan);
        res->removed += removed;
        MP_LOGI("scan done: %d seen, +%d added, %d updated, %d moved, "
                "%d removed, %d invalid",
                res->total, res->added, res->updated, res->moved,
                res->removed, res->invalid);
    }
    return MUSICPACK_OK;
}
