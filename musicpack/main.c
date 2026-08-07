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
# define PCLOSE _pclose
#else
# include <dirent.h>
# include <sys/stat.h>
# define mkdir_p_one(p) mkdir(p, 0755)
# define POPEN popen
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
   success (has=1), 1 if loudness could not be measured. */
static int
measure_loudness(const char *path, int *has, double *lufs, double *peak,
                 double *duration)
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
            while (musepack_decoder_read(dec, pcm, 1152, &frames) == MUSEPACK_OK)
                musicpack_meter_add_frames(meter, pcm, frames);
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
        snprintf(cmd, sizeof cmd,
                 "ffmpeg -v error -i '%s' -f f32le -ac 2 -ar 44100 - 2>/dev/null",
                 path);
        pipe = POPEN(cmd, "rb");
        if (pipe == 0)
            goto out;
        while ((n = fread(buf, sizeof(float), sizeof buf / sizeof(float), pipe)) > 0) {
            musicpack_meter_add_frames(meter, buf, n / 2);
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
    if (m->release_date != 0)
        printf("Release date: %s\n", m->release_date);
    if (m->musicbrainz_release_id != 0)
        printf("MusicBrainz: %s", m->musicbrainz_release_id);
    if (m->identity_confidence != 0)
        printf(" [%s%s]", m->identity_source != 0 ? m->identity_source : "identity",
               m->identity_confidence);
    if (m->musicbrainz_release_id != 0)
        printf("\n");
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

    printf("Discs: %zu\n", m->disc_count);
    for (d = 0; d < m->disc_count; d++) {
        printf("Disc %d: %zu tracks\n", m->discs[d].disc, m->discs[d].track_count);
        for (t = 0; t < m->discs[d].track_count; t++) {
            print_track(&m->discs[d].tracks[t]);
            track_total++;
        }
    }
    printf("Total tracks: %zu\n", track_total);

    if (m->has_album_loudness)
        printf("Album loudness: %.1f LUFS, %.1f dBTP\n",
               m->album_loudness.lufs, m->album_loudness.true_peak_db);
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
        "       [-d RELEASE_DATE] [-T FILE [-n TRACK_TITLE]]... [-A ARTWORK]\n");
}

static int
cmd_create(int argc, char **argv)
{
    const char *out_dir = 0, *title = 0, *release_date = 0, *artwork = 0;
    create_track tracks[256];
    char *artists[64];
    size_t artist_count = 0, track_count = 0;
    int c;

    for (c = 0; c < (int) (sizeof tracks / sizeof *tracks); c++)
        memset(&tracks[c], 0, sizeof tracks[c]);

    while ((c = getopt(argc, argv, "o:t:a:d:T:n:A:")) != -1) {
        switch (c) {
        case 'o': out_dir = optarg; break;
        case 't': title = optarg; break;
        case 'a': artists[artist_count++] = optarg; break;
        case 'd': release_date = optarg; break;
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
        if (release_date != 0)
            m.release_date = strdup(release_date);

        m.discs = (musicpack_disc *) calloc(1, sizeof *m.discs);
        m.disc_count = 1;
        disc = &m.discs[0];
        disc->disc = 1;
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
            /* duration + loudness (best effort) */
            {
                int has_l;
                double lufs, peak, dur = 0;
                if (measure_loudness(target, &has_l, &lufs, &peak, &dur) == 0) {
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
        free(m.release_date);
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
        "       options: -t TITLE  -a ARTIST  -L (skip loudness measurement)\n");
}

static int
cmd_import(int argc, char **argv)
{
    const char *src = 0, *out_dir = 0, *title = 0;
    char *artists[64];
    size_t artist_count = 0;
    int no_loudness = 0;
    int c;
    char **files = 0;
    size_t file_count = 0, file_cap = 0;
    import_track *tracks = 0;
    size_t track_count = 0, track_cap = 0;
    char *artwork_src = 0, *lyrics_src = 0, *booklet_src = 0;
    musicpack_manifest m;
    char audio_dir[MUSICPACK_PATH_MAX + 2];
    char art_dir[MUSICPACK_PATH_MAX + 2], lyr_dir[MUSICPACK_PATH_MAX + 2];
    char hex[MUSICPACK_SHA256_HEX_SIZE];
    char srcpath[MUSICPACK_PATH_MAX + 2];
    size_t i;
    int bad = 0;

    while ((c = getopt(argc, argv, "o:t:a:L")) != -1) {
        switch (c) {
        case 'o': out_dir = optarg; break;
        case 't': title = optarg; break;
        case 'a': artists[artist_count++] = optarg; break;
        case 'L': no_loudness = 1; break;
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

        split_segments(rel, &first, &rest);
        if (rest != 0) {
            disc = disc_from_dirname(first);
            if (disc == 0)
                continue; /* ignore files under non-disc subdirectories */
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
        if (dot != 0 && (strcmp(dot, ".lrc") == 0 || strcmp(dot, ".txt") == 0)) {
            free(lyrics_src);
            lyrics_src = strdup(files[i]);
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
            track_count++;
        }
    }
    free(files);

    if (track_count == 0) {
        fprintf(stderr, "no audio files found under '%s'\n", src);
        return 1;
    }

    /* sort by (disc, explicit number if present, filename), then renumber
       contiguously within each disc for deterministic, gapless numbering. */
    qsort(tracks, track_count, sizeof *tracks, cmp_import_tracks);
    {
        int cur_disc = 0, seq = 1;
        for (i = 0; i < track_count; i++) {
            if (tracks[i].disc != cur_disc) {
                cur_disc = tracks[i].disc;
                seq = 1;
            }
            tracks[i].number = seq++;
        }
    }

    /* build package */
    memset(&m, 0, sizeof m);
    m.album_title = strdup(title != 0 ? title : src);
    m.album_artists = (musicpack_artist *) calloc(artist_count, sizeof *m.album_artists);
    for (i = 0; i < artist_count; i++)
        m.album_artists[i].name = strdup(artists[i]);
    m.album_artist_count = artist_count;

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
        disc->tracks = (musicpack_track *) realloc(disc->tracks,
                                                   (disc->track_count + 1) * sizeof *disc->tracks);
        t = &disc->tracks[disc->track_count];
        memset(t, 0, sizeof *t);
        t->number = it->number;
        t->title = strdup(it->title);
        snprintf(target, sizeof target, "%s/%02d - %s%s", audio_dir, t->number,
                 it->title, it->ext != 0 ? it->ext : "");
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
            if (measure_loudness(target, &has_l, &lufs, &peak, &dur) == 0 && has_l) {
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
        disc->track_count++;
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
    if (!bad && lyrics_src != 0) {
        char target[MUSICPACK_PATH_MAX + 2];
        const char *base = strrchr(lyrics_src, '/');
        const char *name = base != 0 ? base + 1 : lyrics_src;
        snprintf(target, sizeof target, "%s/%s", lyr_dir, name);
        if (snprintf(srcpath, sizeof srcpath, "%s/%s", src, lyrics_src) >= (int) sizeof srcpath) bad = 1;
        if (!bad && copy_file(srcpath, target) != 0) {
            fprintf(stderr, "cannot copy lyrics\n");
            bad = 1;
        } else {
            m.lyrics = (musicpack_asset *) calloc(1, sizeof *m.lyrics);
            m.lyrics_count = 1;
            m.lyrics[0].path = strdup(target + strlen(out_dir) + 1);
            if (musicpack_sha256_file(target, hex, sizeof hex) == MUSICPACK_OK)
                m.lyrics[0].sha256 = strdup(hex);
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
    musicpack_manifest_free(&m);
    for (i = 0; i < track_count; i++) {
        free(tracks[i].src_rel);
        free(tracks[i].title);
        free(tracks[i].ext);
    }
    free(tracks);
    free(artwork_src);
    free(lyrics_src);
    free(booklet_src);

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
        fprintf(stderr, "usage: musicpack <info|verify|create|import> ...\n");
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
    if (strcmp(cmd, "create") == 0)
        return cmd_create(argc - 1, argv + 1);
    if (strcmp(cmd, "import") == 0)
        return cmd_import(argc - 1, argv + 1);
    return usage_error("unknown command");
}
