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
/// \file main.c
/// `musicpack` CLI: info / verify / create / import.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <musicpack/musicpack.h>
#include <musepack/musepack.h>

#ifdef _WIN32
# include <direct.h>
# define mkdir_p_one(p) _mkdir(p)
# define POPEN _popen
# define POPEN_MODE "rb" /* binary mode matters on Windows */
# define PCLOSE _pclose
#else
# include <dirent.h>
# include <sys/stat.h>
# define mkdir_p_one(p) mkdir(p, 0755)
# define POPEN popen
# define POPEN_MODE "r"  /* POSIX popen only accepts "r"/"w"; "rb" fails on macOS */
# define PCLOSE pclose
#endif

#define ABOUT "musicpack - MusicPack package tool " MUSICPACK_VERSION "\n"

static int usage_error(const char *msg)
{
    fprintf(stderr, "%s: %s\n", ABOUT, msg);
    return 2;
}

/* ------------------------------------------------------------------ */
/* small file helpers                                                  */
/* ------------------------------------------------------------------ */

static int
mkdir_p(const char *path)
{
    char tmp[MUSICPACK_PATH_MAX + 2];
    size_t len = strlen(path), i;

    if (len == 0 || len >= sizeof tmp)
        return -1;
    memcpy(tmp, path, len + 1);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';
    for (i = 1; tmp[i] != '\0'; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir_p_one(tmp) != 0 && errno != EEXIST)
                return -1;
            tmp[i] = '/';
        }
    }
    if (mkdir_p_one(tmp) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int
copy_file(const char *src, const char *dst)
{
    FILE *in, *out;
    char buf[65536];
    size_t n;

    in = fopen(src, "rb");
    if (in == 0)
        return -1;
    out = fopen(dst, "wb");
    if (out == 0) { fclose(in); return -1; }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
    if (ferror(in)) { fclose(in); fclose(out); return -1; }
    fclose(in);
    if (fclose(out) != 0)
        return -1;
    return 0;
}

static int
write_all(const char *path, const char *data)
{
    FILE *f = fopen(path, "wb");
    size_t len = strlen(data);
    int ok;
    if (f == 0)
        return -1;
    ok = (len == 0 || fwrite(data, 1, len, f) == len) && fclose(f) == 0;
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* codec + loudness                                                    */
/* ------------------------------------------------------------------ */

static const char *
codec_for_path(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot == 0)
        return "?";
    if (strcmp(dot, ".mpc") == 0) return "musepack";
    if (strcmp(dot, ".flac") == 0) return "flac";
    if (strcmp(dot, ".wav") == 0) return "wav";
    if (strcmp(dot, ".ogg") == 0) return "ogg";
    return dot + 1;
}

/* Measures integrated loudness + true peak of an audio file. Returns 0 on
   success (has=1), 1 if loudness could not be measured.
   If `album` is non-NULL it holds a package-wide album meter (created lazily
   from the first measured track's format); the same PCM is fed to both the
   per-track meter and the album meter so album loudness is measured over the
   concatenated program, never aggregated from per-track values. */
static int
measure_loudness(const char *path, int *has, double *lufs, double *peak,
                 double *duration, musicpack_meter **album)
{
    musicpack_meter *meter = 0;
    int rc = 1;
    const char *codec = codec_for_path(path);

    *has = 0;
    *lufs = 0;
    *peak = 0;

    if (strcmp(codec, "musepack") == 0) {
        mpc_reader reader;
        musepack_decoder *dec;
        musepack_stream_info info;
        float pcm[1152 * 2];
        uint64_t frames;
        unsigned ch, rate;

        if (mpc_reader_init_stdio(&reader, path) != MPC_STATUS_OK)
            return 1;
        dec = musepack_decoder_open(&reader, 0);
        if (dec == 0) { mpc_reader_exit_stdio(&reader); return 1; }
        memset(&info, 0, sizeof info);
        info.size = sizeof info;
        musepack_decoder_get_stream_info(dec, &info);
        ch = info.channels > 2 ? 2 : info.channels;
        rate = info.sample_rate;
        if (duration != 0)
            *duration = (double) musepack_decoder_length_samples(dec) / (double) rate;
        meter = musicpack_meter_new(ch, rate, 0);
        if (meter != 0) {
            if (album != 0 && *album == 0)
                *album = musicpack_meter_new(ch, rate, 0);
            while (musepack_decoder_read(dec, pcm, 1152, &frames) == MUSEPACK_OK) {
                musicpack_meter_add_frames(meter, pcm, frames);
                if (album != 0 && *album != 0)
                    musicpack_meter_add_frames(*album, pcm, frames);
            }
            rc = 0;
        }
        musepack_decoder_close(dec);
        mpc_reader_exit_stdio(&reader);
    } else {
        /* decode via ffmpeg to interleaved f32le stereo 44.1k */
        char cmd[4096];
        FILE *pipe;
        float buf[8192];
        size_t n;
        double total_frames = 0;

        meter = musicpack_meter_new(2, 44100, 0);
        if (meter == 0)
            return 1;
        if (album != 0 && *album == 0)
            *album = musicpack_meter_new(2, 44100, 0);
        snprintf(cmd, sizeof cmd,
                 "ffmpeg -v error -i '%s' -f f32le -ac 2 -ar 44100 - 2>/dev/null",
                 path);
        pipe = POPEN(cmd, POPEN_MODE);
        if (pipe == 0)
            goto out;
        while ((n = fread(buf, sizeof(float), sizeof buf / sizeof(float), pipe)) > 0) {
            musicpack_meter_add_frames(meter, buf, n / 2);
            if (album != 0 && *album != 0)
                musicpack_meter_add_frames(*album, buf, n / 2);
            total_frames += n / 2;
        }
        if (PCLOSE(pipe) != 0)
            goto out;
        if (duration != 0)
            *duration = total_frames / 44100.0;
        rc = 0;
    }

    if (rc == 0 && meter != 0) {
        if (musicpack_meter_result(meter, lufs, peak) != MUSICPACK_OK)
            rc = 1;
        else
            *has = 1;
    }
out:
    musicpack_meter_free(meter);
    return rc;
}

/* ------------------------------------------------------------------ */
/* command: info                                                       */
/* ------------------------------------------------------------------ */

static void
print_track(const musicpack_track *t)
{
    printf("  Track %d: %s\n", t->number, t->title);
    printf("    audio: %s (%s)", t->audio.path, codec_for_path(t->audio.path));
    if (t->has_duration)
        printf(", %.1fs", t->duration);
    if (t->audio.sha256 != 0)
        printf(", sha256 %s", t->audio.sha256);
    printf("\n");
    if (t->loudness.present)
        printf("    loudness: %.1f LUFS, %.1f dBTP\n", t->loudness.lufs,
               t->loudness.true_peak_db);
}

/* Medium format summary: "CD", "CD x 2", "CD + Digital". NULL when no medium
   carries a format. */
static const char *
medium_format_display(const musicpack_manifest *m, char *buf, size_t cap)
{
    char seen[8][32];
    size_t seen_count = 0, d, s;

    for (d = 0; d < m->disc_count; d++) {
        const char *f = m->discs[d].format;
        int dup = 0;
        if (f == 0)
            continue;
        for (s = 0; s < seen_count; s++)
            if (strcmp(seen[s], f) == 0) { dup = 1; break; }
        if (!dup && seen_count < sizeof seen / sizeof *seen)
            snprintf(seen[seen_count++], sizeof seen[0], "%s", f);
    }
    if (seen_count == 0)
        return 0;
    if (seen_count == 1)
        snprintf(buf, cap, "%s x %zu", seen[0], m->disc_count);
    else {
        size_t n = 0;
        buf[0] = '\0';
        for (s = 0; s < seen_count; s++) {
            int k = snprintf(buf + n, cap - n, "%s%s", s > 0 ? " + " : "", seen[s]);
            if (k < 0 || (size_t) k >= cap - n)
                break;
            n += (size_t) k;
        }
    }
    return buf;
}

/* Dominant codec across the package: single display name when all tracks
   share a codec, NULL otherwise (unknown or mixed). */
static const char *
package_codec(const musicpack_manifest *m)
{
    size_t d, t, n = 0;
    const char *first = 0;

    for (d = 0; d < m->disc_count; d++)
        for (t = 0; t < m->discs[d].track_count; t++) {
            const char *c = codec_for_path(m->discs[d].tracks[t].audio.path);
            if (n == 0)
                first = c;
            else if (strcmp(first, c) != 0)
                return 0;
            n++;
        }
    if (n == 0 || first == 0)
        return 0;
    if (strcmp(first, "musepack") == 0) return "Musepack SV8";
    if (strcmp(first, "flac") == 0) return "FLAC";
    if (strcmp(first, "wav") == 0) return "WAV";
    if (strcmp(first, "ogg") == 0) return "Ogg Vorbis";
    return first;
}

static int
cmd_info(const char *dir)
{
    musicpack_package *pkg;
    const musicpack_manifest *m;
    musicpack_report rep = { 0, 0 };
    musicpack_status s;
    size_t d, t, i, track_total = 0;

    pkg = musicpack_package_open_dir(dir, &s);
    if (pkg == 0) {
        fprintf(stderr, "cannot open package '%s' (error %d)\n", dir, (int) s);
        return 1;
    }
    m = musicpack_package_manifest(pkg);

    printf("Package: %s\n", dir);
    printf("Album: %s\n", m->album_title);
    for (i = 0; i < m->album_artist_count; i++) {
        if (m->album_artists[i].role != 0)
            printf("Artist: %s (%s)\n", m->album_artists[i].name, m->album_artists[i].role);
        else
            printf("Artist: %s\n", m->album_artists[i].name);
    }
    if (m->release_type != 0)
        printf("Type: %s\n", m->release_type);
    if (m->release.edition != 0)
        printf("Edition: %s\n", m->release.edition);
    if (m->release.release_date != 0)
        printf("Release date: %s\n", m->release.release_date);
    if (m->original_release_date != 0)
        printf("Original release: %s\n", m->original_release_date);
    if (m->release.country != 0)
        printf("Country: %s\n", m->release.country);
    if (m->release.label != 0)
        printf("Label: %s\n", m->release.label);
    if (m->release.catalogue_number != 0)
        printf("Catalogue: %s\n", m->release.catalogue_number);
    {
        char buf[128];
        const char *fmt = medium_format_display(m, buf, sizeof buf);
        if (fmt != 0)
            printf("Medium: %s\n", fmt);
    }
    if (m->barcode != 0)
        printf("Barcode: %s\n", m->barcode);
    if (m->identity_source != 0 || m->identity_confidence != 0)
        printf("Identity: %s%s%s\n",
               m->identity_source != 0 ? m->identity_source : "unknown",
               m->identity_confidence != 0 ? " " : "",
               m->identity_confidence != 0 ? m->identity_confidence : "");
    if (m->musicbrainz_release_group_id != 0)
        printf("MusicBrainz release group: %s\n", m->musicbrainz_release_group_id);
    if (m->musicbrainz_release_id != 0)
        printf("MusicBrainz release: %s\n", m->musicbrainz_release_id);
    if (m->source_type != 0 || m->source_store != 0)
        printf("Source: %s%s%s%s\n",
               m->source_type != 0 ? m->source_type : "unknown",
               m->source_store != 0 ? " (" : "",
               m->source_store != 0 ? m->source_store : "",
               m->source_store != 0 ? ")" : "");
    if (m->genre_count > 0) {
        printf("Genres:");
        for (i = 0; i < m->genre_count; i++)
            printf(" %s", m->genres[i]);
        printf("\n");
    }
    {
        const char *codec = package_codec(m);
        if (codec != 0)
            printf("Codec: %s\n", codec);
    }

    printf("Discs: %zu\n", m->disc_count);
    for (d = 0; d < m->disc_count; d++) {
        printf("Disc %d: %zu tracks\n", m->discs[d].disc, m->discs[d].track_count);
        for (t = 0; t < m->discs[d].track_count; t++) {
            print_track(&m->discs[d].tracks[t]);
            track_total++;
        }
    }
    printf("Total tracks: %zu\n", track_total);

    if (m->has_album_loudness) {
        printf("Album loudness: %.1f LUFS, %.1f dBTP",
               m->album_loudness.lufs, m->album_loudness.true_peak_db);
        if (m->loudness_algorithm != 0)
            printf(" (%s)", m->loudness_algorithm);
        printf("\n");
    }
    if (m->provenance_tool != 0)
        printf("Provenance: %s %s\n", m->provenance_tool,
               m->provenance_tool_version != 0 ? m->provenance_tool_version : "");

    s = musicpack_package_verify(pkg, &rep, 0, 0);
    printf("Integrity: %s (%zu errors, %zu warnings)\n",
           s == MUSICPACK_OK ? "OK" : "FAILED", rep.errors, rep.warnings);

    musicpack_package_close(pkg);
    return s == MUSICPACK_OK ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* command: verify                                                     */
/* ------------------------------------------------------------------ */

static void
verify_report(void *ctx, const char *message, int is_error)
{
    (void) ctx;
    printf("%s%s\n", is_error ? "error: " : "warning: ", message);
}

static int
cmd_verify(const char *dir, int quiet)
{
    musicpack_package *pkg;
    musicpack_report rep = { 0, 0 };
    musicpack_status s;

    pkg = musicpack_package_open_dir(dir, 0);
    if (pkg == 0) {
        fprintf(stderr, "cannot open package '%s'\n", dir);
        return 1;
    }
    s = musicpack_package_verify(pkg, &rep, quiet ? 0 : verify_report, 0);
    if (!quiet)
        printf("verify: %zu error(s), %zu warning(s)\n", rep.errors, rep.warnings);
    musicpack_package_close(pkg);
    return s == MUSICPACK_OK ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* command: identify                                                   */
/* ------------------------------------------------------------------ */

static int
all_digits(const char *s)
{
    if (s == 0 || *s == '\0')
        return 0;
    for (; *s != '\0'; s++)
        if (*s < '0' || *s > '9')
            return 0;
    return 1;
}

static int
valid_uuid(const char *s)
{
    size_t n = strlen(s), i;
    if (n != 36)
        return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-')
                return 0;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

static char *
read_file_bounded(const char *path, size_t max, musicpack_status *status)
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
    if (len < 0 || (size_t) len > max) { fclose(f); *status = MUSICPACK_ERR_INVALID; return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); *status = MUSICPACK_ERR_IO; return 0; }
    buf = (char *) malloc((size_t) len + 1);
    if (buf == 0) { fclose(f); *status = MUSICPACK_ERR_NOMEM; return 0; }
    if (len > 0 && fread(buf, 1, (size_t) len, f) != (size_t) len) {
        free(buf);
        fclose(f);
        *status = MUSICPACK_ERR_IO;
        return 0;
    }
    fclose(f);
    buf[len] = '\0';
    *status = MUSICPACK_OK;
    return buf;
}

/* Fetches a URL into a NUL-terminated buffer (bounded). The URL is built
   only from validated UUIDs / digit barcodes, so no shell metacharacters
   can reach the command. Returns NULL on failure. */
static char *
curl_fetch(const char *url, size_t max)
{
    char cmd[4096];
    FILE *pipe;
    char *buf;
    size_t cap = 65536, len = 0;

    if (snprintf(cmd, sizeof cmd,
                 "curl -s -m 30 --max-filesize %zu -A \"musicpack/%s (https://musicpack.dev)\" \"%s\"",
                 max, MUSICPACK_VERSION, url) >= (int) sizeof cmd)
        return 0;
    pipe = POPEN(cmd, POPEN_MODE);
    if (pipe == 0)
        return 0;
    buf = (char *) malloc(cap);
    if (buf == 0) {
        PCLOSE(pipe);
        return 0;
    }
    for (;;) {
        size_t n;
        if (len + 65536 + 1 > cap) {
            char *nb = (char *) realloc(buf, cap * 2);
            if (nb == 0)
                break;
            buf = nb;
            cap *= 2;
        }
        n = fread(buf + len, 1, 65536, pipe);
        len += n;
        if (n < 65536)
            break;
    }
    PCLOSE(pipe);
    buf[len] = '\0';
    return buf;
}

static void
usage_identify(void)
{
    fprintf(stderr,
        "usage: musicpack identify <package> [--mb-json FILE]\n");
}

static int
cmd_identify(const char *dir, const char *mb_json_path)
{
    musicpack_package *pkg;
    musicpack_manifest *m;
    musicpack_status s;
    const char *conf = "none";
    int changed = 0;

    pkg = musicpack_package_open_dir(dir, &s);
    if (pkg == 0) {
        fprintf(stderr, "cannot open package '%s' (error %d)\n", dir, (int) s);
        return 1;
    }
    m = musicpack_package_manifest_mutable(pkg);

    if (mb_json_path != 0) {
        char *json = read_file_bounded(mb_json_path, 8u * 1024u * 1024u, &s);
        if (json == 0) {
            fprintf(stderr, "cannot read '%s'\n", mb_json_path);
            musicpack_package_close(pkg);
            return 1;
        }
        conf = musicpack_mb_match_confidence(json, m);
        if (strcmp(conf, "none") != 0) {
            musicpack_mb_apply_release(json, m);
            changed = 1;
        }
        free(json);
    } else if (m->musicbrainz_release_id != 0 &&
               valid_uuid(m->musicbrainz_release_id)) {
        char url[512];
        char *json;
        snprintf(url, sizeof url,
                 "https://musicbrainz.org/ws/2/release/%s?inc=artist-credits+labels+recordings+media&fmt=json",
                 m->musicbrainz_release_id);
        json = curl_fetch(url, 1u * 1024u * 1024u);
        if (json == 0) {
            fprintf(stderr, "identify: network lookup failed; identity unchanged\n");
        } else {
            conf = musicpack_mb_match_confidence(json, m);
            if (strcmp(conf, "none") != 0) {
                musicpack_mb_apply_release(json, m);
                changed = 1;
            }
            free(json);
        }
    } else if (m->barcode != 0 && all_digits(m->barcode)) {
        char url[512];
        char *json;
        snprintf(url, sizeof url,
                 "https://musicbrainz.org/ws/2/release/?query=barcode:%s&fmt=json&limit=5",
                 m->barcode);
        json = curl_fetch(url, 1u * 1024u * 1024u);
        if (json == 0) {
            fprintf(stderr, "identify: network lookup failed; identity unchanged\n");
        } else {
            conf = musicpack_mb_match_confidence(json, m);
            if (strcmp(conf, "none") != 0) {
                musicpack_mb_apply_release(json, m);
                changed = 1;
            }
            free(json);
        }
    } else {
        fprintf(stderr,
                "identify: no MusicBrainz release id or barcode to match;\n"
                "         use --mb-json with a release document to apply offline\n");
    }

    if (changed) {
        if (m->identity_source == 0)
            m->identity_source = strdup("musicbrainz");
        if (m->identity_confidence == 0)
            m->identity_confidence = strdup(conf);
        if (musicpack_package_save_manifest(pkg) != MUSICPACK_OK) {
            fprintf(stderr, "identify: cannot save manifest\n");
            musicpack_package_close(pkg);
            return 1;
        }
        printf("identify: %s\n", conf);
    } else {
        printf("identify: no match applied\n");
    }

    musicpack_package_close(pkg);
    return 0;
}

/* ------------------------------------------------------------------ */
/* command: update-metadata                                            */
/* ------------------------------------------------------------------ */

static void
usage_update_metadata(void)
{
    fprintf(stderr,
        "usage: musicpack update-metadata <package> [--sync-tags]\n"
        "       --sync-tags: rewrite APEv2 tags on .mpc tracks from the\n"
        "       manifest and refresh their checksums\n");
}

/* Reads embedded tags from a package audio file (FLAC Vorbis / MPC APEv2). */
static int
read_package_track_tags(const char *apath, musicpack_tag_set *tags)
{
    const char *dot = strrchr(apath, '.');
    if (dot == 0)
        return 0;
    if (strcmp(dot, ".flac") == 0)
        return musicpack_flac_read_metadata(apath, tags, 0) == MUSICPACK_OK;
    if (strcmp(dot, ".mpc") == 0)
        return musicpack_ape_read(apath, tags) == MUSICPACK_OK;
    return 0;
}

static int
cmd_update_metadata(const char *dir, int sync_tags)
{
    musicpack_package *pkg;
    musicpack_manifest *m;
    musicpack_status s;
    size_t d, t;
    int album_done = 0, hash_changed = 0, reconciled = 0;

    pkg = musicpack_package_open_dir(dir, &s);
    if (pkg == 0) {
        fprintf(stderr, "cannot open package '%s'\n", dir);
        return 1;
    }
    m = musicpack_package_manifest_mutable(pkg);

    /* album-level: fill empty fields from the first audio track's tags */
    for (d = 0; d < m->disc_count && !album_done; d++)
        for (t = 0; t < m->discs[d].track_count && !album_done; t++) {
            char apath[MUSICPACK_PATH_MAX + 2];
            musicpack_tag_set tags;
            if (musicpack_package_track_path(pkg, d, t, apath, sizeof apath)
                != MUSICPACK_OK)
                continue;
            memset(&tags, 0, sizeof tags);
            if (read_package_track_tags(apath, &tags)) {
                if (musicpack_tag_map_album(&tags, m) == MUSICPACK_OK)
                    reconciled = 1;
                album_done = 1;
            }
            musicpack_tag_set_free(&tags);
        }

    /* per-track reconcile, then optional manifest -> APEv2 re-projection */
    for (d = 0; d < m->disc_count; d++) {
        musicpack_disc *disc = &m->discs[d];
        for (t = 0; t < disc->track_count; t++) {
            musicpack_track *tr = &disc->tracks[t];
            char apath[MUSICPACK_PATH_MAX + 2];
            musicpack_tag_set tags;
            const char *dot;

            if (musicpack_package_track_path(pkg, d, t, apath, sizeof apath)
                != MUSICPACK_OK)
                continue;
            memset(&tags, 0, sizeof tags);
            if (read_package_track_tags(apath, &tags)) {
                if (musicpack_tag_map_track(&tags, tr) == MUSICPACK_OK)
                    reconciled = 1;
            }
            musicpack_tag_set_free(&tags);

            dot = strrchr(apath, '.');
            if (sync_tags && dot != 0 && strcmp(dot, ".mpc") == 0) {
                musicpack_tag_set ape;
                char hex[MUSICPACK_SHA256_HEX_SIZE];
                if (musicpack_manifest_to_ape_tags(m, tr, disc->disc,
                                                   (int) m->disc_count,
                                                   (int) disc->track_count,
                                                   &ape) == MUSICPACK_OK) {
                    if (musicpack_ape_write(apath, &ape) == MUSICPACK_OK &&
                        musicpack_sha256_file(apath, hex, sizeof hex)
                            == MUSICPACK_OK) {
                        free(tr->audio.sha256);
                        tr->audio.sha256 = strdup(hex);
                        hash_changed = 1;
                    } else {
                        fprintf(stderr,
                                "update-metadata: cannot re-tag '%s'\n",
                                tr->audio.path);
                    }
                    musicpack_tag_set_free(&ape);
                }
            } else if (sync_tags && dot != 0 && strcmp(dot, ".flac") == 0) {
                fprintf(stderr,
                        "update-metadata: --sync-tags only writes APEv2 (.mpc); "
                        "skipping '%s'\n", tr->audio.path);
            }
        }
    }

    if (reconciled || hash_changed) {
        if (musicpack_package_save_manifest(pkg) != MUSICPACK_OK) {
            fprintf(stderr, "update-metadata: cannot save manifest\n");
            musicpack_package_close(pkg);
            return 1;
        }
        printf("update-metadata: manifest updated (%s)\n",
               hash_changed ? "tags synced and checksums refreshed" : "reconciled");
    } else {
        printf("update-metadata: no changes\n");
    }

    musicpack_package_close(pkg);
    return 0;
}

/* ------------------------------------------------------------------ */
/* command: create                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    char *path;
    char *title;
    int has_title;
} create_track;

static void
usage_create(void)
{
    fprintf(stderr,
        "usage: musicpack create -o <dir> -t TITLE [-a ARTIST]...\n"
        "       [-d RELEASE_DATE] [-R RELEASE_TYPE] [-O ORIGINAL_RELEASE_DATE]\n"
        "       [-e EDITION] [-l LABEL] [-c CATALOGUE] [-C COUNTRY]\n"
        "       [-m MEDIUM_FORMAT] [-N NOTES] [-T FILE [-n TRACK_TITLE]]... [-A ARTWORK]\n");
}

static int
cmd_create(int argc, char **argv)
{
    const char *out_dir = 0, *title = 0, *release_date = 0, *artwork = 0;
    const char *release_type = 0, *orig_release_date = 0, *edition = 0;
    const char *label = 0, *catalogue = 0, *country = 0, *medium_format = 0;
    const char *notes = 0;
    create_track tracks[256];
    char *artists[64];
    size_t artist_count = 0, track_count = 0;
    musicpack_meter *album_meter = 0;
    int c;

    for (c = 0; c < (int) (sizeof tracks / sizeof *tracks); c++)
        memset(&tracks[c], 0, sizeof tracks[c]);

    while ((c = getopt(argc, argv, "o:t:a:d:R:O:e:l:c:C:m:N:T:n:A:")) != -1) {
        switch (c) {
        case 'o': out_dir = optarg; break;
        case 't': title = optarg; break;
        case 'a': artists[artist_count++] = optarg; break;
        case 'd': release_date = optarg; break;
        case 'R': release_type = optarg; break;
        case 'O': orig_release_date = optarg; break;
        case 'e': edition = optarg; break;
        case 'l': label = optarg; break;
        case 'c': catalogue = optarg; break;
        case 'C': country = optarg; break;
        case 'm': medium_format = optarg; break;
        case 'N': notes = optarg; break;
        case 'T':
            if (track_count >= sizeof tracks / sizeof *tracks)
                return usage_error("too many tracks");
            tracks[track_count].path = optarg;
            track_count++;
            break;
        case 'n':
            if (track_count == 0)
                return usage_error("--track-title must follow --track");
            tracks[track_count - 1].title = optarg;
            tracks[track_count - 1].has_title = 1;
            break;
        case 'A': artwork = optarg; break;
        default:
            usage_create();
            return 2;
        }
    }
    if (out_dir == 0 || title == 0 || track_count == 0) {
        usage_create();
        return 2;
    }

    /* build the model */
    {
        musicpack_manifest m;
        char audio_dir[MUSICPACK_PATH_MAX + 2];
        char art_dir[MUSICPACK_PATH_MAX + 2];
        musicpack_disc *disc;
        char hex[MUSICPACK_SHA256_HEX_SIZE];
        size_t i;
        int bad = 0;

        memset(&m, 0, sizeof m);
        m.album_title = strdup(title);
        m.album_artists = (musicpack_artist *) calloc(artist_count, sizeof *m.album_artists);
        for (i = 0; i < artist_count; i++) {
            m.album_artists[i].name = strdup(artists[i]);
        }
        m.album_artist_count = artist_count;
        if (release_type != 0)
            m.release_type = strdup(release_type);
        if (orig_release_date != 0)
            m.original_release_date = strdup(orig_release_date);
        if (release_date != 0) {
            m.release.present = 1;
            m.release.release_date = strdup(release_date);
        }
        if (edition != 0) { m.release.present = 1; m.release.edition = strdup(edition); }
        if (label != 0) { m.release.present = 1; m.release.label = strdup(label); }
        if (catalogue != 0) { m.release.present = 1; m.release.catalogue_number = strdup(catalogue); }
        if (country != 0) { m.release.present = 1; m.release.country = strdup(country); }
        if (notes != 0) { m.release.present = 1; m.release.notes = strdup(notes); }

        m.discs = (musicpack_disc *) calloc(1, sizeof *m.discs);
        m.disc_count = 1;
        disc = &m.discs[0];
        disc->disc = 1;
        if (medium_format != 0)
            disc->format = strdup(medium_format);
        disc->tracks = (musicpack_track *) calloc(track_count, sizeof *disc->tracks);
        disc->track_count = track_count;

        snprintf(audio_dir, sizeof audio_dir, "%s/audio", out_dir);
        snprintf(art_dir, sizeof art_dir, "%s/artwork", out_dir);
        if (mkdir_p(out_dir) != 0 || mkdir_p(audio_dir) != 0 || mkdir_p(art_dir) != 0) {
            fprintf(stderr, "cannot create package directory '%s'\n", out_dir);
            return 1;
        }

        for (i = 0; i < track_count; i++) {
            musicpack_track *t = &disc->tracks[i];
            const char *base = strrchr(tracks[i].path, '/');
            const char *dot;
            char target[MUSICPACK_PATH_MAX + 2];

            base = base != 0 ? base + 1 : tracks[i].path;
            dot = strrchr(base, '.');
            {
                size_t stem_len = dot != 0 ? (size_t) (dot - base) : strlen(base);
                t->number = (int) i + 1;
                t->title = strdup(tracks[i].has_title ? tracks[i].title : "");
                if (!t->title || t->title[0] == '\0') {
                    free(t->title);
                    t->title = (char *) malloc(stem_len + 1);
                    memcpy(t->title, base, stem_len);
                    t->title[stem_len] = '\0';
                }
                snprintf(target, sizeof target, "%s/audio/%02d - %s%s",
                         out_dir, t->number, t->title, dot != 0 ? dot : "");
            }
            /* copy + hash */
            if (copy_file(tracks[i].path, target) != 0) {
                fprintf(stderr, "cannot copy '%s'\n", tracks[i].path);
                bad = 1;
                break;
            }
            t->audio.path = strdup(target + strlen(out_dir) + 1);
            if (musicpack_sha256_file(target, hex, sizeof hex) != MUSICPACK_OK) {
                fprintf(stderr, "cannot hash '%s'\n", target);
                bad = 1;
                break;
            }
            t->audio.sha256 = strdup(hex);
            /* duration + loudness (best effort); also feeds the album meter */
            {
                int has_l;
                double lufs, peak, dur = 0;
                if (measure_loudness(target, &has_l, &lufs, &peak, &dur,
                                     &album_meter) == 0) {
                    if (has_l) {
                        t->loudness.present = 1;
                        t->loudness.lufs = lufs;
                        t->loudness.true_peak_db = peak;
                    }
                    if (dur > 0) {
                        t->has_duration = 1;
                        t->duration = dur;
                    }
                }
            }
        }

        if (!bad && artwork != 0) {
            char target[MUSICPACK_PATH_MAX + 2];
            const char *ext = strrchr(artwork, '.');
            snprintf(target, sizeof target, "%s/artwork/front%s",
                     out_dir, ext != 0 ? ext : ".jpg");
            if (copy_file(artwork, target) != 0) {
                fprintf(stderr, "cannot copy artwork '%s'\n", artwork);
                bad = 1;
            } else {
                m.artwork = (musicpack_artwork *) calloc(1, sizeof *m.artwork);
                m.artwork_count = 1;
                m.artwork[0].role = strdup("front");
                m.artwork[0].asset.path = strdup(target + strlen(out_dir) + 1);
                if (musicpack_sha256_file(target, hex, sizeof hex) == MUSICPACK_OK)
                    m.artwork[0].asset.sha256 = strdup(hex);
            }
        }

        if (!bad && album_meter != 0) {
            double alufs, apeak;
            if (musicpack_meter_result(album_meter, &alufs, &apeak) == MUSICPACK_OK) {
                m.has_album_loudness = 1;
                m.album_loudness.lufs = alufs;
                m.album_loudness.true_peak_db = apeak;
                m.loudness_algorithm = strdup(MUSICPACK_LOUDNESS_STANDARD);
            }
        }

        if (!bad) {
            char *json = 0;
            if (musicpack_manifest_write(&m, &json) == MUSICPACK_OK) {
                char mpath[MUSICPACK_PATH_MAX + 2];
                snprintf(mpath, sizeof mpath, "%s/manifest.json", out_dir);
                if (write_all(mpath, json) != 0)
                    bad = 1;
                free(json);
            } else {
                bad = 1;
            }
        }

        /* free model */
        for (i = 0; i < m.album_artist_count; i++)
            free(m.album_artists[i].name);
        free(m.album_artists);
        free(m.album_title);
        free(m.release_type);
        free(m.original_release_date);
        free(m.release.release_date);
        free(m.release.edition);
        free(m.release.country);
        free(m.release.label);
        free(m.release.catalogue_number);
        free(m.release.notes);
        free(disc->format);
        free(m.loudness_algorithm);
        musicpack_meter_free(album_meter);
        for (i = 0; i < disc->track_count; i++) {
            free(disc->tracks[i].title);
            free(disc->tracks[i].audio.path);
            free(disc->tracks[i].audio.sha256);
        }
        free(disc->tracks);
        free(m.discs);
        for (i = 0; i < m.artwork_count; i++) {
            free(m.artwork[i].role);
            free(m.artwork[i].asset.path);
            free(m.artwork[i].asset.sha256);
        }
        free(m.artwork);

        if (bad) {
            fprintf(stderr, "create failed\n");
            return 1;
        }
    }
    printf("created package '%s'\n", out_dir);
    return 0;
}

/* ------------------------------------------------------------------ */
/* command: import                                                     */
/* ------------------------------------------------------------------ */

#if defined(_WIN32)
# include <io.h>
typedef struct _finddata_t finddata_t;
static int walk_push(char ***files, size_t *count, size_t *cap, const char *rel);
#endif

static int
walk_push(char ***files, size_t *count, size_t *cap, const char *rel)
{
    char *copy;
    if (*count >= *cap) {
        size_t newcap = *cap == 0 ? 64 : *cap * 2;
        char **nf = (char **) realloc(*files, newcap * sizeof *nf);
        if (nf == 0)
            return -1;
        *files = nf;
        *cap = newcap;
    }
    copy = strdup(rel);
    if (copy == 0)
        return -1;
    (*files)[(*count)++] = copy;
    return 0;
}

static void
walk_dir(const char *abs, const char *rel, char ***files, size_t *count, size_t *cap)
{
#if defined(_WIN32)
    char pat[MUSICPACK_PATH_MAX + 2];
    finddata_t fd;
    intptr_t h;

    snprintf(pat, sizeof pat, "%s/*", abs);
    h = _findfirst(pat, &fd);
    if (h == -1)
        return;
    do {
        char next[MUSICPACK_PATH_MAX + 2], relnext[MUSICPACK_PATH_MAX + 2];
        if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0)
            continue;
        snprintf(next, sizeof next, "%s/%s", abs, fd.name);
        if (fd.attrib & _A_SUBDIR) {
            if (rel[0] == '\0') snprintf(relnext, sizeof relnext, "%s", fd.name);
            else snprintf(relnext, sizeof relnext, "%s/%s", rel, fd.name);
            walk_dir(next, relnext, files, count, cap);
        } else {
            if (rel[0] == '\0') snprintf(relnext, sizeof relnext, "%s", fd.name);
            else snprintf(relnext, sizeof relnext, "%s/%s", rel, fd.name);
            walk_push(files, count, cap, relnext);
        }
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
#else
    DIR *d = opendir(abs);
    struct dirent *e;
    if (d == 0)
        return;
    while ((e = readdir(d)) != 0) {
        char next[MUSICPACK_PATH_MAX + 2], relnext[MUSICPACK_PATH_MAX + 2];
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        snprintf(next, sizeof next, "%s/%s", abs, e->d_name);
        if (lstat(next, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (rel[0] == '\0') snprintf(relnext, sizeof relnext, "%s", e->d_name);
            else snprintf(relnext, sizeof relnext, "%s/%s", rel, e->d_name);
            walk_dir(next, relnext, files, count, cap);
        } else if (S_ISREG(st.st_mode)) {
            if (rel[0] == '\0') snprintf(relnext, sizeof relnext, "%s", e->d_name);
            else snprintf(relnext, sizeof relnext, "%s/%s", rel, e->d_name);
            walk_push(files, count, cap, relnext);
        }
    }
    closedir(d);
#endif
}

static int
is_audio_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == 0)
        return 0;
    return strcmp(dot, ".mpc") == 0 || strcmp(dot, ".flac") == 0 ||
           strcmp(dot, ".wav") == 0 || strcmp(dot, ".ogg") == 0;
}

/* disc number from a directory name like "disc-2" / "CD 1" (0 if not a disc). */
static int
disc_from_dirname(const char *name)
{
    const char *p = name;
    int n = 0, digits = 0;
    if (strncmp(p, "disc", 4) != 0 && strncmp(p, "cd", 2) != 0)
        return 0;
    p += (strncmp(p, "disc", 4) == 0) ? 4 : 2;
    while (*p == '-' || *p == '_' || *p == ' ')
        p++;
    while (*p >= '0' && *p <= '9') {
        n = n * 10 + (*p - '0');
        digits++;
        p++;
    }
    return digits > 0 ? n : 0;
}

static void
split_segments(const char *rel, const char **first, const char **rest)
{
    const char *slash = strchr(rel, '/');
    if (slash != 0) {
        *first = rel;
        *rest = slash + 1;
    } else {
        *first = rel;
        *rest = 0;
    }
}

typedef struct {
    int disc;
    int number;
    char *src_rel;   /* relative to source root */
    char *title;     /* without extension or number prefix */
    char *ext;       /* original file extension incl. dot (".mpc") */
    musicpack_tag_set tags;  /* embedded metadata; empty when untagged */
    musicpack_pictures pics; /* embedded FLAC pictures; empty when none */
    int has_tags;
    char *lyric_path;        /* written lyrics asset (manifest-relative) */
    char *lyric_sha;
} import_track;

static int
cmp_import_tracks(const void *a, const void *b)
{
    const import_track *ta = (const import_track *) a;
    const import_track *tb = (const import_track *) b;
    int na = ta->number > 0 ? ta->number : 0x7fffffff;
    int nb = tb->number > 0 ? tb->number : 0x7fffffff;
    if (ta->disc != tb->disc)
        return ta->disc - tb->disc;
    if (na != nb)
        return na - nb;
    return strcmp(ta->src_rel, tb->src_rel);
}

static void
usage_import(void)
{
    fprintf(stderr,
        "usage: musicpack import -o <dir> [options] <source-dir>\n"
        "       options: -t TITLE  -a ARTIST  -L (skip loudness measurement)\n"
        "       release: -d RELEASE_DATE  -R RELEASE_TYPE  -O ORIGINAL_RELEASE_DATE\n"
        "                -e EDITION  -l LABEL  -c CATALOGUE  -C COUNTRY\n"
        "                -m MEDIUM_FORMAT  -N NOTES\n");
}

/* Reads embedded metadata into it->tags (best-effort: read failures leave the
   set empty and has_tags=0). FLAC uses Vorbis Comments, Musepack uses APEv2. */
static void
read_track_tags(import_track *it, const char *srcpath)
{
    const char *dot = strrchr(it->src_rel, '.');

    it->has_tags = 0;
    if (dot == 0)
        return;
    if (strcmp(dot, ".flac") == 0) {
        if (musicpack_flac_read_metadata(srcpath, &it->tags, &it->pics) == MUSICPACK_OK)
            it->has_tags = 1;
    } else if (strcmp(dot, ".mpc") == 0) {
        if (musicpack_ape_read(srcpath, &it->tags) == MUSICPACK_OK)
            it->has_tags = 1;
    }
}

/* Reads a positive integer tag field, accepting the Vorbis and APEv2 key
   spellings. Returns 1 on success. */
static int
tag_int_field(const musicpack_tag_set *tags, const char *vorbis_key,
              const char *ape_key, int *out)
{
    const musicpack_tag *t = musicpack_tag_set_get(tags, vorbis_key);
    if (t == 0 || t->is_binary)
        t = musicpack_tag_set_get(tags, ape_key);
    if (t == 0 || t->is_binary)
        return 0;
    return musicpack_meta_parse_track_number(t->value, out);
}

/* Replaces characters that are illegal inside a single path component. */
static void
sanitize_component(char *s)
{
    for (; *s != '\0'; s++)
        if (*s == '/' || *s == '\\' || *s == ':')
            *s = '-';
}

/* Writes raw bytes (not NUL-terminated) to a file. */
static int
write_bytes(const char *path, const unsigned char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == 0)
        return -1;
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0)
        return -1;
    return 0;
}

static const char *
ext_for_mime(const char *mime)
{
    if (mime == 0)
        return ".img";
    if (strcmp(mime, "image/jpeg") == 0) return ".jpg";
    if (strcmp(mime, "image/png") == 0) return ".png";
    if (strcmp(mime, "image/gif") == 0) return ".gif";
    if (strcmp(mime, "image/webp") == 0) return ".webp";
    if (strcmp(mime, "image/bmp") == 0) return ".bmp";
    return ".img";
}

static int
artwork_role_taken(const musicpack_manifest *m, const char *role)
{
    size_t i;
    for (i = 0; i < m->artwork_count; i++)
        if (strcmp(m->artwork[i].role, role) == 0)
            return 1;
    return 0;
}

static int
manifest_add_artwork(musicpack_manifest *m, const char *role,
                     const char *relpath, const char *sha)
{
    musicpack_artwork *na =
        (musicpack_artwork *) realloc(m->artwork,
                                      (m->artwork_count + 1) * sizeof *na);
    if (na == 0)
        return -1;
    m->artwork = na;
    m->artwork[m->artwork_count].role = strdup(role);
    m->artwork[m->artwork_count].asset.path = strdup(relpath);
    m->artwork[m->artwork_count].asset.sha256 = sha != 0 ? strdup(sha) : 0;
    if (m->artwork[m->artwork_count].role == 0 ||
        m->artwork[m->artwork_count].asset.path == 0 ||
        (sha != 0 && m->artwork[m->artwork_count].asset.sha256 == 0))
        return -1;
    m->artwork_count++;
    return 0;
}

static int
cmd_import(int argc, char **argv)
{
    const char *src = 0, *out_dir = 0, *title = 0;
    const char *release_date = 0, *release_type = 0, *orig_release_date = 0;
    const char *edition = 0, *label = 0, *catalogue = 0, *country = 0;
    const char *medium_format = 0, *notes = 0;
    char *artists[64];
    size_t artist_count = 0;
    int no_loudness = 0;
    int c;
    char **files = 0;
    size_t file_count = 0, file_cap = 0;
    import_track *tracks = 0;
    size_t track_count = 0, track_cap = 0;
    char *artwork_src = 0, *booklet_src = 0;
    char **lyrics_srcs = 0;
    size_t lyrics_count = 0, lyrics_cap = 0;
    char **extras_srcs = 0;
    size_t extras_count = 0, extras_cap = 0;
    musicpack_manifest m;
    musicpack_meter *album_meter = 0;
    char audio_dir[MUSICPACK_PATH_MAX + 2];
    char art_dir[MUSICPACK_PATH_MAX + 2], lyr_dir[MUSICPACK_PATH_MAX + 2];
    char hex[MUSICPACK_SHA256_HEX_SIZE];
    char srcpath[MUSICPACK_PATH_MAX + 2];
    size_t i;
    int bad = 0;

    while ((c = getopt(argc, argv, "o:t:a:Ld:R:O:e:l:c:C:m:N:")) != -1) {
        switch (c) {
        case 'o': out_dir = optarg; break;
        case 't': title = optarg; break;
        case 'a': artists[artist_count++] = optarg; break;
        case 'L': no_loudness = 1; break;
        case 'd': release_date = optarg; break;
        case 'R': release_type = optarg; break;
        case 'O': orig_release_date = optarg; break;
        case 'e': edition = optarg; break;
        case 'l': label = optarg; break;
        case 'c': catalogue = optarg; break;
        case 'C': country = optarg; break;
        case 'm': medium_format = optarg; break;
        case 'N': notes = optarg; break;
        default: usage_import(); return 2;
        }
    }
    if (optind < argc)
        src = argv[optind];
    if (src == 0 || out_dir == 0) {
        usage_import();
        return 2;
    }

    walk_dir(src, "", &files, &file_count, &file_cap);
    track_cap = 64;
    tracks = (import_track *) calloc(track_cap, sizeof *tracks);

    /* classify files */
    for (i = 0; i < file_count; i++) {
        const char *rel = files[i];
        const char *first, *rest, *dot;
        int disc = 1;
        int from_dir = 0;

        split_segments(rel, &first, &rest);
        if (rest != 0) {
            disc = disc_from_dirname(first);
            if (disc == 0)
                continue; /* ignore files under non-disc subdirectories */
            from_dir = 1;
            rel = rest;
        }
        if (strcmp(rel, "cover.jpg") == 0 || strcmp(rel, "cover.png") == 0 ||
            strcmp(rel, "front.jpg") == 0 || strcmp(rel, "front.png") == 0 ||
            strcmp(rel, "folder.jpg") == 0) {
            free(artwork_src);
            artwork_src = strdup(files[i]);
            continue;
        }
        if (strcmp(rel, "booklet.pdf") == 0) {
            free(booklet_src);
            booklet_src = strdup(files[i]);
            continue;
        }
        dot = strrchr(rel, '.');
        if (dot != 0 && strcmp(dot, ".lrc") == 0) {
            if (lyrics_count >= lyrics_cap) {
                lyrics_cap = lyrics_cap == 0 ? 8 : lyrics_cap * 2;
                lyrics_srcs = (char **) realloc(lyrics_srcs, lyrics_cap * sizeof *lyrics_srcs);
            }
            lyrics_srcs[lyrics_count++] = strdup(files[i]);
            continue;
        }
        if (dot != 0 && (strcmp(dot, ".txt") == 0 || strcmp(dot, ".md") == 0)) {
            if (extras_count >= extras_cap) {
                extras_cap = extras_cap == 0 ? 8 : extras_cap * 2;
                extras_srcs = (char **) realloc(extras_srcs, extras_cap * sizeof *extras_srcs);
            }
            extras_srcs[extras_count++] = strdup(files[i]);
            continue;
        }
        if (is_audio_ext(rel)) {
            if (track_count >= track_cap) {
                track_cap *= 2;
                tracks = (import_track *) realloc(tracks, track_cap * sizeof *tracks);
            }
            tracks[track_count].src_rel = strdup(files[i]);
            tracks[track_count].disc = disc;
            /* leading digits = track number */
            {
                const char *base = rest != 0 ? rest : first;
                int n = 0, digits = 0;
                while (base[n] >= '0' && base[n] <= '9') {
                    n++;
                    digits++;
                }
                tracks[track_count].number = digits > 0 ? atoi(base) : 0;
                /* title: strip "NN - " or "NN. " prefix and extension */
                {
                    const char *t = base;
                    size_t stem_len;
                    char *stem;
                    if (digits > 0) {
                        t = base + n;
                        while (*t == ' ' || *t == '-' || *t == '.')
                            t++;
                    }
                    dot = strrchr(t, '.');
                    stem_len = dot != 0 ? (size_t) (dot - t) : strlen(t);
                    stem = (char *) malloc(stem_len + 1);
                    memcpy(stem, t, stem_len);
                    stem[stem_len] = '\0';
                    tracks[track_count].title = stem;
                }
                {
                    const char *dot2 = strrchr(base, '.');
                    if (dot2 != 0)
                        tracks[track_count].ext = strdup(dot2);
                    else
                        tracks[track_count].ext = strdup("");
                }
            }
            /* embedded metadata takes precedence over filename heuristics */
            snprintf(srcpath, sizeof srcpath, "%s/%s", src, files[i]);
            read_track_tags(&tracks[track_count], srcpath);
            if (tracks[track_count].has_tags) {
                import_track *it = &tracks[track_count];
                const musicpack_tag *tv;
                int num;
                if (tag_int_field(&it->tags, "TRACKNUMBER", "Track", &num))
                    it->number = num;
                tv = musicpack_tag_set_get(&it->tags, "TITLE");
                if (tv != 0 && !tv->is_binary && tv->value != 0 && *tv->value != '\0') {
                    free(it->title);
                    it->title = strdup(tv->value);
                }
                if (!from_dir &&
                    tag_int_field(&it->tags, "DISCNUMBER", "Disc", &num))
                    it->disc = num;
            }
            track_count++;
        }
    }
    free(files);

    if (track_count == 0) {
        fprintf(stderr, "no audio files found under '%s'\n", src);
        return 1;
    }

    /* sort by (disc, explicit number if present, filename). Renumber a disc
       contiguously only when it has tracks without explicit numbers; explicit
       numbers from tags are authoritative and preserved. */
    qsort(tracks, track_count, sizeof *tracks, cmp_import_tracks);
    i = 0;
    while (i < track_count) {
        int disc = tracks[i].disc;
        size_t j = i;
        int all_have = 1;
        while (j < track_count && tracks[j].disc == disc) {
            if (tracks[j].number <= 0)
                all_have = 0;
            j++;
        }
        if (!all_have) {
            int seq = 1;
            size_t k;
            for (k = i; k < j; k++)
                tracks[k].number = seq++;
        }
        i = j;
    }

    /* consistency: duplicate (disc, track) falls back to renumbering; album
       title conflicts are reported as warnings */
    i = 0;
    while (i < track_count) {
        int disc = tracks[i].disc;
        size_t j = i, k, l;
        int dup = 0;
        while (j < track_count && tracks[j].disc == disc)
            j++;
        for (k = i; k < j; k++)
            for (l = k + 1; l < j; l++)
                if (tracks[k].number > 0 && tracks[k].number == tracks[l].number)
                    dup = 1;
        if (dup) {
            int seq = 1;
            fprintf(stderr, "warning: duplicate track numbers on disc %d; renumbering\n",
                    disc);
            for (k = i; k < j; k++)
                tracks[k].number = seq++;
        }
        i = j;
    }
    if (track_count > 1 && tracks[0].has_tags) {
        const musicpack_tag *a0 = musicpack_tag_set_get(&tracks[0].tags, "ALBUM");
        if (a0 != 0 && !a0->is_binary) {
            for (i = 1; i < track_count; i++) {
                const musicpack_tag *ai;
                if (!tracks[i].has_tags)
                    continue;
                ai = musicpack_tag_set_get(&tracks[i].tags, "ALBUM");
                if (ai != 0 && !ai->is_binary && strcmp(a0->value, ai->value) != 0)
                    fprintf(stderr,
                            "warning: conflicting album names ('%s' vs '%s')\n",
                            a0->value, ai->value);
            }
        }
    }

    /* build package: explicit flags first, then embedded metadata fills the
       gaps (first-wins), then the folder name is the title fallback. */
    memset(&m, 0, sizeof m);
    if (title != 0)
        m.album_title = strdup(title);
    m.album_artists = (musicpack_artist *) calloc(artist_count, sizeof *m.album_artists);
    for (i = 0; i < artist_count; i++)
        m.album_artists[i].name = strdup(artists[i]);
    m.album_artist_count = artist_count;
    if (release_type != 0)
        m.release_type = strdup(release_type);
    if (orig_release_date != 0)
        m.original_release_date = strdup(orig_release_date);
    if (release_date != 0) {
        m.release.present = 1;
        m.release.release_date = strdup(release_date);
    }
    if (edition != 0) { m.release.present = 1; m.release.edition = strdup(edition); }
    if (label != 0) { m.release.present = 1; m.release.label = strdup(label); }
    if (catalogue != 0) { m.release.present = 1; m.release.catalogue_number = strdup(catalogue); }
    if (country != 0) { m.release.present = 1; m.release.country = strdup(country); }
    if (notes != 0) { m.release.present = 1; m.release.notes = strdup(notes); }

    if (track_count > 0 && tracks[0].has_tags) {
        musicpack_status st = musicpack_tag_map_album(&tracks[0].tags, &m);
        if (st != MUSICPACK_OK) {
            fprintf(stderr, "cannot read album metadata\n");
            bad = 1;
            goto cleanup;
        }
    }
    if (m.album_title == 0)
        m.album_title = strdup(src);

    snprintf(audio_dir, sizeof audio_dir, "%s/audio", out_dir);
    snprintf(art_dir, sizeof art_dir, "%s/artwork", out_dir);
    snprintf(lyr_dir, sizeof lyr_dir, "%s/lyrics", out_dir);
    if (mkdir_p(out_dir) != 0 || mkdir_p(audio_dir) != 0 ||
        mkdir_p(art_dir) != 0 || mkdir_p(lyr_dir) != 0) {
        fprintf(stderr, "cannot create package directory '%s'\n", out_dir);
        return 1;
    }

    /* count discs */
    {
        size_t d;
        int ndiscs = 0;
        for (d = 0; d < track_count; d++)
            if (tracks[d].disc > ndiscs)
                ndiscs = tracks[d].disc;
        m.discs = (musicpack_disc *) calloc((size_t) ndiscs, sizeof *m.discs);
        m.disc_count = (size_t) ndiscs;
    }

    for (i = 0; i < track_count; i++) {
        import_track *it = &tracks[i];
        musicpack_disc *disc;
        musicpack_track *t;
        char target[MUSICPACK_PATH_MAX + 2];

        if ((size_t) it->disc - 1 >= m.disc_count) {
            bad = 1;
            break;
        }
        disc = &m.discs[it->disc - 1];
        disc->disc = it->disc;
        if (disc->format == 0 && medium_format != 0)
            disc->format = strdup(medium_format);
        disc->tracks = (musicpack_track *) realloc(disc->tracks,
                                                   (disc->track_count + 1) * sizeof *disc->tracks);
        t = &disc->tracks[disc->track_count];
        memset(t, 0, sizeof *t);
        if (it->has_tags) {
            musicpack_status st = musicpack_tag_map_track(&it->tags, t);
            if (st != MUSICPACK_OK) {
                fprintf(stderr, "cannot read track metadata\n");
                bad = 1;
                break;
            }
        }
        if (t->number == 0)
            t->number = it->number;
        if (t->title == 0)
            t->title = strdup(it->title);
        {
            char fname[MUSICPACK_PATH_MAX + 2];
            snprintf(fname, sizeof fname, "%s", t->title != 0 ? t->title : "");
            sanitize_component(fname);
            snprintf(target, sizeof target, "%s/%02d - %s%s", audio_dir, t->number,
                     fname, it->ext != 0 ? it->ext : "");
        }
        snprintf(srcpath, sizeof srcpath, "%s/%s", src, it->src_rel);

        if (copy_file(srcpath, target) != 0) {
            fprintf(stderr, "cannot copy '%s'\n", srcpath);
            bad = 1;
            break;
        }
        t->audio.path = strdup(target + strlen(out_dir) + 1);
        if (musicpack_sha256_file(target, hex, sizeof hex) != MUSICPACK_OK) {
            fprintf(stderr, "cannot hash '%s'\n", target);
            bad = 1;
            break;
        }
        t->audio.sha256 = strdup(hex);
        if (!no_loudness) {
            int has_l;
            double lufs, peak, dur = 0;
            if (measure_loudness(target, &has_l, &lufs, &peak, &dur,
                                 &album_meter) == 0 && has_l) {
                t->loudness.present = 1;
                t->loudness.lufs = lufs;
                t->loudness.true_peak_db = peak;
                if (dur > 0) {
                    t->has_duration = 1;
                    t->duration = dur;
                }
            } else {
                fprintf(stderr, "warning: could not measure loudness of '%s'\n", it->src_rel);
            }
        }
        /* unsynchronized lyrics tag -> first-class lyrics asset */
        if (it->has_tags) {
            const musicpack_tag *ly = musicpack_tag_set_get(&it->tags, "LYRICS");
            if (ly == 0 || ly->is_binary)
                ly = musicpack_tag_set_get(&it->tags, "UNSYNCEDLYRICS");
            if (ly != 0 && !ly->is_binary && ly->value != 0 && *ly->value != '\0') {
                char lpath[MUSICPACK_PATH_MAX + 2];
                char fname2[MUSICPACK_PATH_MAX + 2];
                snprintf(fname2, sizeof fname2, "%s", t->title != 0 ? t->title : "");
                sanitize_component(fname2);
                snprintf(lpath, sizeof lpath, "%s/%02d - %s.txt", lyr_dir, t->number,
                         fname2);
                if (write_all(lpath, ly->value) == 0) {
                    it->lyric_path = strdup(lpath + strlen(out_dir) + 1);
                    if (musicpack_sha256_file(lpath, hex, sizeof hex) == MUSICPACK_OK)
                        it->lyric_sha = strdup(hex);
                }
            }
        }
        disc->track_count++;
    }

    /* embedded artwork: local cover files (below) win; otherwise FLAC
       pictures and APEv2 cover art fill missing roles, first per role. */
    if (!bad) {
        size_t t_i;
        char target[MUSICPACK_PATH_MAX + 2];
        for (t_i = 0; t_i < track_count; t_i++) {
            import_track *it = &tracks[t_i];
            size_t k;
            for (k = 0; k < it->pics.count; k++) {
                const musicpack_picture *pic = &it->pics.items[k];
                const char *role = musicpack_meta_picture_role(pic->type);
                const char *ext;
                char rel[MUSICPACK_PATH_MAX + 2];
                if (artwork_role_taken(&m, role))
                    continue;
                ext = ext_for_mime(pic->mime);
                snprintf(target, sizeof target, "%s/%s%s", art_dir, role, ext);
                if (write_bytes(target, pic->data, pic->data_len) != 0) {
                    fprintf(stderr, "cannot write artwork\n");
                    bad = 1;
                    break;
                }
                snprintf(rel, sizeof rel, "artwork/%s%s", role, ext);
                if (musicpack_sha256_file(target, hex, sizeof hex) == MUSICPACK_OK)
                    manifest_add_artwork(&m, role, rel, hex);
                else
                    manifest_add_artwork(&m, role, rel, 0);
            }
            if (it->has_tags) {
                const musicpack_tag *cov =
                    musicpack_tag_set_get(&it->tags, "Cover Art (Front)");
                if (cov != 0 && cov->is_binary && cov->binary_len > 0 &&
                    !artwork_role_taken(&m, "front")) {
                    const unsigned char *nul =
                        (const unsigned char *) memchr(cov->binary, '\0',
                                                       cov->binary_len);
                    const unsigned char *img = nul != 0 ? nul + 1 : cov->binary;
                    size_t img_len = nul != 0
                        ? cov->binary_len - (size_t) (nul - cov->binary) - 1
                        : cov->binary_len;
                    const char *fname = (const char *) cov->binary;
                    const char *dot = strrchr(fname, '.');
                    const char *ext = dot != 0 ? dot : ".img";
                    char rel[MUSICPACK_PATH_MAX + 2];
                    if (img_len == 0)
                        continue;
                    snprintf(target, sizeof target, "%s/front%s", art_dir, ext);
                    if (write_bytes(target, img, img_len) != 0) {
                        fprintf(stderr, "cannot write artwork\n");
                        bad = 1;
                        break;
                    }
                    snprintf(rel, sizeof rel, "artwork/front%s", ext);
                    if (musicpack_sha256_file(target, hex, sizeof hex) == MUSICPACK_OK)
                        manifest_add_artwork(&m, "front", rel, hex);
                    else
                        manifest_add_artwork(&m, "front", rel, 0);
                }
            }
        }
    }

    if (!bad && artwork_src != 0) {
        char target[MUSICPACK_PATH_MAX + 2];
        const char *ext = strrchr(artwork_src, '.');
        snprintf(target, sizeof target, "%s/front%s", art_dir, ext != 0 ? ext : ".jpg");
        if (snprintf(srcpath, sizeof srcpath, "%s/%s", src, artwork_src) >= (int) sizeof srcpath) bad = 1;
        if (!bad && copy_file(srcpath, target) != 0) {
            fprintf(stderr, "cannot copy artwork\n");
            bad = 1;
        } else {
            m.artwork = (musicpack_artwork *) calloc(1, sizeof *m.artwork);
            m.artwork_count = 1;
            m.artwork[0].role = strdup("front");
            m.artwork[0].asset.path = strdup(target + strlen(out_dir) + 1);
            if (musicpack_sha256_file(target, hex, sizeof hex) == MUSICPACK_OK)
                m.artwork[0].asset.sha256 = strdup(hex);
        }
    }
    if (!bad && booklet_src != 0) {
        char target[MUSICPACK_PATH_MAX + 2];
        snprintf(target, sizeof target, "%s/booklet", out_dir);
        if (mkdir_p(target) != 0)
            bad = 1;
        else {
            snprintf(target, sizeof target, "%s/booklet/booklet.pdf", out_dir);
            if (snprintf(srcpath, sizeof srcpath, "%s/%s", src, booklet_src) >= (int) sizeof srcpath) bad = 1;
            if (!bad && copy_file(srcpath, target) != 0) {
                fprintf(stderr, "cannot copy booklet\n");
                bad = 1;
            } else {
                m.booklet = (musicpack_asset *) calloc(1, sizeof *m.booklet);
                m.booklet_count = 1;
                m.booklet[0].path = strdup(target + strlen(out_dir) + 1);
                if (musicpack_sha256_file(target, hex, sizeof hex) == MUSICPACK_OK)
                    m.booklet[0].sha256 = strdup(hex);
            }
        }
    }
    if (!bad && lyrics_count > 0) {
        size_t k;
        m.lyrics = (musicpack_asset *) calloc(lyrics_count, sizeof *m.lyrics);
        m.lyrics_count = lyrics_count;
        for (k = 0; k < lyrics_count && !bad; k++) {
            char target[MUSICPACK_PATH_MAX + 2];
            const char *base = strrchr(lyrics_srcs[k], '/');
            const char *name = base != 0 ? base + 1 : lyrics_srcs[k];
            snprintf(target, sizeof target, "%s/%s", lyr_dir, name);
            if (snprintf(srcpath, sizeof srcpath, "%s/%s", src, lyrics_srcs[k]) >= (int) sizeof srcpath) { bad = 1; break; }
            if (copy_file(srcpath, target) != 0) {
                fprintf(stderr, "cannot copy lyrics '%s'\n", name);
                bad = 1;
                break;
            }
            m.lyrics[k].path = strdup(target + strlen(out_dir) + 1);
            if (musicpack_sha256_file(target, hex, sizeof hex) == MUSICPACK_OK)
                m.lyrics[k].sha256 = strdup(hex);
        }
    }
    /* merge tag-derived lyrics assets into the manifest (ownership moves);
       lyrics_count keeps counting only the source .lrc files so cleanup stays
       correct */
    if (!bad) {
        size_t tag_count = 0, src_n = lyrics_count, mcount;
        for (i = 0; i < track_count; i++)
            if (tracks[i].lyric_path != 0)
                tag_count++;
        if (tag_count > 0) {
            musicpack_asset *na = (musicpack_asset *) realloc(
                m.lyrics, (src_n + tag_count) * sizeof *na);
            if (na == 0)
                bad = 1;
            else {
                m.lyrics = na;
                mcount = src_n;
                for (i = 0; i < track_count; i++) {
                    if (tracks[i].lyric_path != 0) {
                        musicpack_asset *a = &m.lyrics[mcount++];
                        a->path = tracks[i].lyric_path;
                        a->sha256 = tracks[i].lyric_sha;
                        tracks[i].lyric_path = 0;
                        tracks[i].lyric_sha = 0;
                    }
                }
                m.lyrics_count = mcount;
            }
        }
    }
    if (!bad && extras_count > 0) {
        size_t k;
        char ex_dir[MUSICPACK_PATH_MAX + 2];
        snprintf(ex_dir, sizeof ex_dir, "%s/extras", out_dir);
        if (mkdir_p(ex_dir) != 0)
            bad = 1;
        else {
            m.extras = (musicpack_asset *) calloc(extras_count, sizeof *m.extras);
            m.extras_count = extras_count;
            for (k = 0; k < extras_count && !bad; k++) {
                char target[MUSICPACK_PATH_MAX + 2];
                const char *base = strrchr(extras_srcs[k], '/');
                const char *name = base != 0 ? base + 1 : extras_srcs[k];
                snprintf(target, sizeof target, "%s/%s", ex_dir, name);
                if (snprintf(srcpath, sizeof srcpath, "%s/%s", src, extras_srcs[k]) >= (int) sizeof srcpath) { bad = 1; break; }
                if (copy_file(srcpath, target) != 0) {
                    fprintf(stderr, "cannot copy extra '%s'\n", name);
                    bad = 1;
                    break;
                }
                m.extras[k].path = strdup(target + strlen(out_dir) + 1);
                if (musicpack_sha256_file(target, hex, sizeof hex) == MUSICPACK_OK)
                    m.extras[k].sha256 = strdup(hex);
            }
        }
    }

    if (!no_loudness && album_meter != 0) {
        double alufs, apeak;
        if (musicpack_meter_result(album_meter, &alufs, &apeak) == MUSICPACK_OK) {
            m.has_album_loudness = 1;
            m.album_loudness.lufs = alufs;
            m.album_loudness.true_peak_db = apeak;
            m.loudness_algorithm = strdup(MUSICPACK_LOUDNESS_STANDARD);
        }
    }

    if (!bad) {
        char *json = 0;
        if (musicpack_manifest_write(&m, &json) == MUSICPACK_OK) {
            char mpath[MUSICPACK_PATH_MAX + 2];
            snprintf(mpath, sizeof mpath, "%s/manifest.json", out_dir);
            if (write_all(mpath, json) != 0)
                bad = 1;
            free(json);
        } else {
            bad = 1;
        }
    }

cleanup:
    /* free model */
    musicpack_manifest_clear(&m);
    musicpack_meter_free(album_meter);
    for (i = 0; i < track_count; i++) {
        musicpack_tag_set_free(&tracks[i].tags);
        musicpack_pictures_free(&tracks[i].pics);
        free(tracks[i].lyric_path);
        free(tracks[i].lyric_sha);
        free(tracks[i].src_rel);
        free(tracks[i].title);
        free(tracks[i].ext);
    }
    free(tracks);
    free(artwork_src);
    free(booklet_src);
    for (i = 0; i < lyrics_count; i++)
        free(lyrics_srcs[i]);
    free(lyrics_srcs);
    for (i = 0; i < extras_count; i++)
        free(extras_srcs[i]);
    free(extras_srcs);

    if (bad) {
        fprintf(stderr, "import failed\n");
        return 1;
    }
    printf("imported %zu track(s) into '%s'\n", track_count, out_dir);
    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int
main(int argc, char **argv)
{
    const char *cmd;

    fprintf(stderr, "%s", ABOUT);
    if (argc < 2) {
        fprintf(stderr, "usage: musicpack <info|verify|identify|create|import|update-metadata> ...\n");
        return 2;
    }
    cmd = argv[1];
    if (strcmp(cmd, "info") == 0)
        return argc >= 3 ? cmd_info(argv[2]) : usage_error("info requires a package");
    if (strcmp(cmd, "verify") == 0) {
        int quiet = 0, i;
        for (i = 2; i < argc; i++)
            if (strcmp(argv[i], "-q") == 0)
                quiet = 1;
        return argc >= 3 ? cmd_verify(argv[2], quiet) : usage_error("verify requires a package");
    }
    if (strcmp(cmd, "identify") == 0) {
        const char *dir = 0, *mbjson = 0;
        int i;
        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--mb-json") == 0 && i + 1 < argc) {
                mbjson = argv[i + 1];
                i++;
            } else if (dir == 0) {
                dir = argv[i];
            } else {
                return usage_error("too many arguments");
            }
        }
        if (dir == 0) {
            usage_identify();
            return 2;
        }
        return cmd_identify(dir, mbjson);
    }
    if (strcmp(cmd, "create") == 0)
        return cmd_create(argc - 1, argv + 1);
    if (strcmp(cmd, "import") == 0)
        return cmd_import(argc - 1, argv + 1);
    if (strcmp(cmd, "update-metadata") == 0) {
        const char *dir = 0;
        int sync = 0, i;
        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--sync-tags") == 0)
                sync = 1;
            else if (dir == 0)
                dir = argv[i];
            else
                return usage_error("too many arguments");
        }
        if (dir == 0) {
            usage_update_metadata();
            return 2;
        }
        return cmd_update_metadata(dir, sync);
    }
    return usage_error("unknown command");
}
