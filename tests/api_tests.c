/*
 * API tests for the stable libmusepack decoder interface.
 *
 * Exercises the public musepack_* API: lifecycle, invalid input, memory- and
 * file-backed readers, stream info, full decode, seeking (beginning/middle/
 * near end/repeated), end-of-stream and multiple independent instances.
 *
 * Usage: api_tests <fixture-a.mpc> <fixture-b.mpc>
 * Wired into CTest as the "api" suite (see tests/CMakeLists.txt).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <musepack/musepack.h>

static int failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);    \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static unsigned char *load_file(const char *path, size_t *size)
{
    FILE *f;
    long n;
    unsigned char *buf;

    f = fopen(path, "rb");
    if (f == 0) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    buf = malloc((size_t) n);
    if (buf == 0) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t) n, f) != (size_t) n) {
        free(buf); fclose(f); return 0;
    }
    fclose(f);
    *size = (size_t) n;
    return buf;
}

static uint64_t decode_all(musepack_decoder *d, uint64_t *last_frames)
{
    static float pcm[MUSEPACK_FRAME_MAX * 2];
    uint64_t total = 0, frames;
    musepack_error e;

    for (;;) {
        e = musepack_decoder_read(d, pcm, MUSEPACK_FRAME_MAX, &frames);
        if (e == MUSEPACK_ERR_EOF)
            break;
        if (e != MUSEPACK_OK)
            return total;
        total += frames;
    }
    if (last_frames != 0)
        *last_frames = frames;
    return total;
}

/* Deterministic PCM checksum: sum of raw float bits mod 2^32. */
static unsigned long pcm_checksum(musepack_decoder *d)
{
    static float pcm[MUSEPACK_FRAME_MAX * 2];
    unsigned long sum = 0;
    uint64_t frames;

    musepack_decoder_seek_sample(d, 0);
    while (musepack_decoder_read(d, pcm, MUSEPACK_FRAME_MAX, &frames) == MUSEPACK_OK) {
        uint64_t i;
        for (i = 0; i < frames; i++) {
            unsigned int bits;
            memcpy(&bits, &pcm[i], sizeof bits);
            sum += bits;
        }
    }
    return sum;
}

/* ------------------------------------------------------------------ */
/* create / destroy lifecycle                                          */
/* ------------------------------------------------------------------ */
static void test_lifecycle(const char *fixture)
{
    size_t sz;
    unsigned char *data = load_file(fixture, &sz);
    mpc_reader reader;
    musepack_error err = MUSEPACK_OK;
    musepack_decoder *d;
    int i;

    CHECK(data != 0, "load fixture A");
    if (data == 0) return;

    for (i = 0; i < 3; i++) {
        mpc_reader_init_memory(&reader, data, sz);
        d = musepack_decoder_open(&reader, &err);
        CHECK(d != 0, "open succeeds");
        CHECK(err == MUSEPACK_OK, "open error is OK");
        if (d != 0)
            musepack_decoder_close(d);
        mpc_reader_exit_memory(&reader);
    }
    free(data);
}

/* ------------------------------------------------------------------ */
/* invalid input                                                       */
/* ------------------------------------------------------------------ */
static void test_invalid_input(void)
{
    const char garbage[] = "this is definitely not a musepack file at all";
    const char truncated[] = "MPCK";
    mpc_reader reader;
    musepack_error err = MUSEPACK_OK;
    musepack_decoder *d;

    mpc_reader_init_memory(&reader, garbage, sizeof garbage - 1);
    d = musepack_decoder_open(&reader, &err);
    CHECK(d == 0, "garbage rejected");
    CHECK(err != MUSEPACK_OK, "garbage reports an error");
    mpc_reader_exit_memory(&reader);

    mpc_reader_init_memory(&reader, truncated, sizeof truncated - 1);
    d = musepack_decoder_open(&reader, &err);
    CHECK(d == 0, "truncated magic rejected");
    mpc_reader_exit_memory(&reader);

    err = MUSEPACK_OK;
    d = musepack_decoder_open(0, &err);
    CHECK(d == 0, "NULL reader rejected");
    CHECK(err == MUSEPACK_ERR_INVALID, "NULL reader error code");
}

/* ------------------------------------------------------------------ */
/* stream info                                                         */
/* ------------------------------------------------------------------ */
static void test_streaminfo(const char *fixture, unsigned int freq,
                            unsigned int channels, unsigned int version,
                            unsigned long samples)
{
    size_t sz;
    unsigned char *data = load_file(fixture, &sz);
    mpc_reader reader;
    mpc_streaminfo si;
    musepack_decoder *d;
    musepack_error err;

    CHECK(data != 0, "load fixture for info");
    if (data == 0) return;
    mpc_reader_init_memory(&reader, data, sz);
    d = musepack_decoder_open(&reader, &err);
    CHECK(d != 0, "open for info");
    if (d == 0) { free(data); return; }

    CHECK(musepack_decoder_get_info(d, &si) == MUSEPACK_OK, "get_info ok");
    CHECK(si.sample_freq == freq, "sample_freq matches");
    CHECK(si.channels == channels, "channels match");
    CHECK(si.stream_version == version, "stream version matches");
    CHECK(musepack_decoder_length_samples(d) == samples, "length samples matches");

    musepack_decoder_close(d);
    mpc_reader_exit_memory(&reader);
    free(data);
}

/* ------------------------------------------------------------------ */
/* full decode: memory- and file-backed, deterministic                 */
/* ------------------------------------------------------------------ */
static void test_full_decode(const char *fixture, unsigned long samples)
{
    size_t sz;
    unsigned char *data = load_file(fixture, &sz);
    mpc_reader reader;
    musepack_decoder *d;
    musepack_error err;
    uint64_t total, last;
    unsigned long c1, c2;

    /* memory-backed */
    mpc_reader_init_memory(&reader, data, sz);
    d = musepack_decoder_open(&reader, &err);
    CHECK(d != 0, "open memory-backed");
    if (d != 0) {
        total = decode_all(d, &last);
        CHECK(total == samples, "memory decode sample count");
        CHECK(total == musepack_decoder_position(d), "position == decoded count");
        err = musepack_decoder_read(d, 0, 1, 0);
        CHECK(err == MUSEPACK_ERR_INVALID, "read after EOF rejects NULL pcm");
        musepack_decoder_close(d);
    }
    mpc_reader_exit_memory(&reader);

    /* file-backed */
    {
        mpc_reader freader;
        CHECK(mpc_reader_init_stdio(&freader, fixture) == MPC_STATUS_OK,
              "init stdio reader");
        d = musepack_decoder_open(&freader, &err);
        CHECK(d != 0, "open file-backed");
        if (d != 0) {
            total = decode_all(d, 0);
            CHECK(total == samples, "file decode sample count");
            musepack_decoder_close(d);
        }
        mpc_reader_exit_stdio(&freader);
    }

    /* determinism: two decodes of the same fixture hash identically */
    mpc_reader_init_memory(&reader, data, sz);
    d = musepack_decoder_open(&reader, &err);
    c1 = pcm_checksum(d);
    musepack_decoder_close(d);
    mpc_reader_exit_memory(&reader);
    mpc_reader_init_memory(&reader, data, sz);
    d = musepack_decoder_open(&reader, &err);
    c2 = pcm_checksum(d);
    musepack_decoder_close(d);
    mpc_reader_exit_memory(&reader);
    CHECK(c1 == c2, "decode is deterministic");

    free(data);
}

/* ------------------------------------------------------------------ */
/* seeking                                                             */
/* ------------------------------------------------------------------ */
static void test_seeking(const char *fixture, unsigned long samples,
                         unsigned long freq)
{
    static const unsigned long targets[] = { 0, 4410, 22050, 22050, 44000, 44099 };
    size_t sz;
    unsigned char *data = load_file(fixture, &sz);
    mpc_reader reader;
    musepack_decoder *d;
    musepack_error err;
    unsigned int i;
    unsigned long expected_seconds;

    mpc_reader_init_memory(&reader, data, sz);
    d = musepack_decoder_open(&reader, &err);
    CHECK(d != 0, "open for seek");
    if (d == 0) { free(data); return; }

    /* seek to beginning */
    CHECK(musepack_decoder_seek_sample(d, 0) == MUSEPACK_OK, "seek 0 ok");
    CHECK(musepack_decoder_position(d) == 0, "position after seek 0");
    CHECK(decode_all(d, 0) == samples, "decode from 0 == full length");

    /* seek to middle, near end, repeated */
    for (i = 0; i < sizeof targets / sizeof *targets; i++) {
        unsigned long t = targets[i];
        uint64_t rest;
        if (t >= samples) t = samples;
        err = musepack_decoder_seek_sample(d, t);
        CHECK(err == MUSEPACK_OK, "seek ok");
        CHECK(musepack_decoder_position(d) == t, "position after seek");
        rest = decode_all(d, 0);
        CHECK(rest == samples - t, "decode after seek reaches the end");
        CHECK(musepack_decoder_position(d) == samples, "position at end after seek");
    }

    /* out-of-range seek clamps to the length */
    CHECK(musepack_decoder_seek_sample(d, samples + 10000) == MUSEPACK_OK,
          "oversized seek ok");
    CHECK(musepack_decoder_position(d) == samples, "oversized seek clamps");
    CHECK(decode_all(d, 0) == 0, "decode at clamped end is empty");

    /* seek by seconds */
    expected_seconds = (unsigned long) (0.1 * (double) freq + 0.5);
    CHECK(musepack_decoder_seek_seconds(d, 0.1) == MUSEPACK_OK, "seek seconds ok");
    CHECK(musepack_decoder_position(d) == expected_seconds, "seek seconds position");

    musepack_decoder_close(d);
    mpc_reader_exit_memory(&reader);
    free(data);
}

/* ------------------------------------------------------------------ */
/* multiple independent instances                                      */
/* ------------------------------------------------------------------ */
static void test_multiple_instances(const char *fa, unsigned long sa,
                                    const char *fb, unsigned long sb)
{
    size_t za, zb;
    unsigned char *da = load_file(fa, &za);
    unsigned char *db = load_file(fb, &zb);
    mpc_reader ra1, ra2, rb1;
    musepack_decoder *da1, *da2, *db1;
    musepack_error err;
    uint64_t total;

    /* Each decoder gets its own reader so instances are fully independent. */
    mpc_reader_init_memory(&ra1, da, za);
    mpc_reader_init_memory(&ra2, da, za);
    mpc_reader_init_memory(&rb1, db, zb);
    da1 = musepack_decoder_open(&ra1, &err);
    da2 = musepack_decoder_open(&ra2, &err);   /* second, independent instance */
    db1 = musepack_decoder_open(&rb1, &err);
    CHECK(da1 != 0 && da2 != 0 && db1 != 0, "three instances open");

    /* interleave: decode a bit of each, alternate */
    {
        static float pcm[MUSEPACK_FRAME_MAX * 2];
        uint64_t f1 = 0, f2 = 0, f3 = 0, n;
        while (musepack_decoder_read(da1, pcm, MUSEPACK_FRAME_MAX, &n) == MUSEPACK_OK)
            f1 += n;
        CHECK(f1 == sa, "instance 1 full decode");
        while (musepack_decoder_read(db1, pcm, MUSEPACK_FRAME_MAX, &n) == MUSEPACK_OK)
            f2 += n;
        CHECK(f2 == sb, "instance 2 (other file) full decode");
        musepack_decoder_seek_sample(da2, 0);
        while (musepack_decoder_read(da2, pcm, MUSEPACK_FRAME_MAX, &n) == MUSEPACK_OK)
            f3 += n;
        CHECK(f3 == sa, "instance 3 (repeat of file A) full decode");
    }

    total = decode_all(da1, 0);
    CHECK(total == 0, "instance 1 already exhausted");
    musepack_decoder_close(da1);
    musepack_decoder_close(da2);
    musepack_decoder_close(db1);
    mpc_reader_exit_memory(&ra1);
    mpc_reader_exit_memory(&ra2);
    mpc_reader_exit_memory(&rb1);
    free(da);
    free(db);
}

static unsigned long get_length(const char *fixture)
{
    size_t sz;
    unsigned char *data = load_file(fixture, &sz);
    mpc_reader reader;
    musepack_decoder *d;
    musepack_error err;
    unsigned long length;

    if (data == 0) return 0;
    mpc_reader_init_memory(&reader, data, sz);
    d = musepack_decoder_open(&reader, &err);
    if (d == 0) { free(data); return 0; }
    length = (unsigned long) musepack_decoder_length_samples(d);
    musepack_decoder_close(d);
    mpc_reader_exit_memory(&reader);
    free(data);
    return length;
}

int main(int argc, char **argv)
{
    const char *fa, *fb;
    unsigned long sa, sb;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <fixture-a.mpc> <fixture-b.mpc>\n", argv[0]);
        return 2;
    }
    fa = argv[1];
    fb = argv[2];
    sa = get_length(fa);
    sb = get_length(fb);
    if (sa == 0 || sb == 0) {
        fprintf(stderr, "cannot read fixture lengths\n");
        return 1;
    }

    test_lifecycle(fa);
    test_invalid_input();
    test_streaminfo(fa, 44100, 2, 8, 44100);
    test_full_decode(fa, sa);
    test_seeking(fa, sa, 44100);
    test_multiple_instances(fa, sa, fb, sb);

    if (failures) {
        fprintf(stderr, "%d api test(s) failed\n", failures);
        return 1;
    }
    printf("all api tests passed\n");
    return 0;
}
