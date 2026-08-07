/*
 * C tests for libmusicpack: manifest parse/validate, unknown-field
 * round-trip, path security, sha256, BS.1770 meter, determinism, package
 * open/verify, and the Musepack handoff.
 *
 * Usage: mpack_tests <musicpack-album.mpack> <flac-album.mpack>
 * Wired into CTest as the "mpack" suite.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
# define _USE_MATH_DEFINES
# include <windows.h>
# include <direct.h>
#else
# include <unistd.h> /* mkdtemp */
#endif

#include <musicpack/musicpack.h>
#include <musepack/musepack.h>

static int failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);    \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static int
make_temp_dir(char *buf, size_t cap)
{
#if defined(_WIN32)
    const char *base = getenv("TEMP");
    if (base == 0) base = ".";
    if (snprintf(buf, cap, "%s\\mpack_test_%lu", base,
                 (unsigned long) GetCurrentProcessId()) >= (int) cap)
        return -1;
    if (_mkdir(buf) != 0 && errno != EEXIST)
        return -1;
    return 0;
#else
    if (snprintf(buf, cap, "/tmp/mpack_test_XXXXXX") >= (int) cap)
        return -1;
    return mkdtemp(buf) != 0 ? 0 : -1;
#endif
}

static void
remove_temp_dir(const char *dir, const char *file)
{
    char path[512];
    if (file != 0) {
        snprintf(path, sizeof path, "%s/%s", dir, file);
        remove(path);
    }
#if defined(_WIN32)
    _rmdir(dir);
#else
    remove(dir);
#endif
}

/* ------------------------------------------------------------------ */
/* manifest parse / validate                                            */
/* ------------------------------------------------------------------ */

static const char *VALID_MANIFEST =
    "{"
    "  \"format\": \"musicpack\","
    "  \"version\": 1,"
    "  \"album\": {"
    "    \"title\": \"Test Album\","
    "    \"artists\": [ {\"name\": \"A\", \"role\": \"main\"}, {\"name\": \"B\"} ]"
    "  },"
    "  \"media\": [ {"
    "    \"disc\": 1,"
    "    \"tracks\": [ {"
    "      \"track\": 1,"
    "      \"title\": \"T1\","
    "      \"audio\": { \"path\": \"audio/01 - T1.mpc\", \"sha256\": \""
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\" }"
    "    } ]"
    "  } ]"
    "}";

static void
test_parse_valid(void)
{
    musicpack_manifest *m;
    musicpack_status s;

    m = musicpack_manifest_parse(VALID_MANIFEST, &s);
    CHECK(m != 0, "valid manifest parses");
    if (m == 0)
        return;
    CHECK(strcmp(m->album_title, "Test Album") == 0, "album title");
    CHECK(m->album_artist_count == 2, "two album artists");
    CHECK(strcmp(m->album_artists[0].role, "main") == 0, "artist role");
    CHECK(m->disc_count == 1 && m->discs[0].track_count == 1, "one disc one track");
    CHECK(m->discs[0].tracks[0].number == 1, "track number");
    musicpack_manifest_free(m);
}

static void
test_parse_invalid(void)
{
    musicpack_status s;

    CHECK(musicpack_manifest_parse("not json{", &s) == 0 && s == MUSICPACK_ERR_JSON,
          "malformed json rejected");
    CHECK(musicpack_manifest_parse(
              "{\"format\":\"other\",\"version\":1}", &s) == 0,
          "wrong format rejected");
    CHECK(musicpack_manifest_parse(
              "{\"format\":\"musicpack\",\"version\":2}", &s) == 0
          && s == MUSICPACK_ERR_VERSION, "unsupported version rejected");
    CHECK(musicpack_manifest_parse(
              "{\"format\":\"musicpack\",\"version\":1}", &s) == 0,
          "missing album rejected");
    CHECK(musicpack_manifest_parse(
              "{\"format\":\"musicpack\",\"version\":1,\"album\":{"
              "\"artists\":[{\"name\":\"A\"}]},"
              "\"media\":[{\"disc\":1,\"tracks\":[]}]}", &s) == 0,
          "empty tracks rejected");
    CHECK(musicpack_manifest_parse(
              "{\"format\":\"musicpack\",\"version\":1,\"album\":{"
              "\"title\":\"T\",\"artists\":[{\"name\":\"A\"}]},"
              "\"media\":[{\"disc\":1,\"tracks\":[{"
              "\"track\":1,\"title\":\"T\",\"audio\":{\"path\":\"../x.mpc\"}}]}]}", &s) == 0,
          "traversal audio path rejected");
    CHECK(musicpack_manifest_parse(
              "{\"format\":\"musicpack\",\"version\":1,\"album\":{"
              "\"title\":\"T\",\"artists\":[{\"name\":\"A\"}]},"
              "\"media\":[{\"disc\":1,\"tracks\":[{"
              "\"track\":1,\"title\":\"T\",\"audio\":{\"path\":\"a.mpc\","
              "\"sha256\":\"ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ"
              "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ\"}}]}]}", &s) == 0,
          "invalid sha256 rejected");
}

/* ------------------------------------------------------------------ */
/* unknown-field round-trip preservation                               */
/* ------------------------------------------------------------------ */

static void
test_unknown_field_roundtrip(void)
{
    char dir[512];
    char path[512];
    char *json, *readback;
    FILE *f;

    if (make_temp_dir(dir, sizeof dir) != 0) {
        CHECK(0, "make temp dir");
        return;
    }
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    json = strdup(
        "{\"format\":\"musicpack\",\"version\":1,"
        "\"xFutureField\":{\"note\":\"survives\"},"
        "\"album\":{\"title\":\"R\",\"artists\":[{\"name\":\"A\"}]},"
        "\"release\":{\"edition\":\"Original\",\"xReleaseExt\":\"keep me\"},"
        "\"media\":[{\"disc\":1,\"tracks\":[{"
        "\"track\":1,\"title\":\"T\","
        "\"xTrackExt\":\"keep me\","
        "\"audio\":{\"path\":\"audio/a.mpc\","
        "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}]}]}");
    f = fopen(path, "wb");
    CHECK(f != 0, "write manifest");
    if (f != 0) {
        fwrite(json, 1, strlen(json), f);
        fclose(f);
    }

    {
        musicpack_package *pkg = musicpack_package_open_dir(dir, 0);
        CHECK(pkg != 0, "open roundtrip package");
        if (pkg != 0) {
            CHECK(musicpack_package_save_manifest(pkg) == MUSICPACK_OK, "save manifest");
            musicpack_package_close(pkg);
        }
    }
    readback = malloc(65536);
    {
        size_t n = 0;
        FILE *r = fopen(path, "rb");
        CHECK(r != 0, "read back manifest");
        if (r != 0) {
            n = fread(readback, 1, 65535, r);
            readback[n] = '\0';
            fclose(r);
        }
    }
    CHECK(strstr(readback, "xFutureField") != 0, "unknown top-level field preserved");
    CHECK(strstr(readback, "xTrackExt") != 0, "unknown track field preserved");
    CHECK(strstr(readback, "xReleaseExt") != 0, "unknown release field preserved");
    CHECK(strstr(readback, "survives") != 0, "unknown nested value preserved");
    CHECK(strstr(readback, "keep me") != 0, "unknown release value preserved");

    free(readback);
    free(json);
    remove_temp_dir(dir, "manifest.json");
}

/* ------------------------------------------------------------------ */
/* multi-disc / multi-value artists                                    */
/* ------------------------------------------------------------------ */

static void
test_multidisc(void)
{
    const char *j =
        "{\"format\":\"musicpack\",\"version\":1,"
        "\"album\":{\"title\":\"MD\",\"artists\":[{\"name\":\"A\"}]},"
        "\"media\":["
        "{\"disc\":1,\"tracks\":[{\"track\":1,\"title\":\"a\","
        "\"audio\":{\"path\":\"audio/a1.mpc\",\"sha256\":\""
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}]},"
        "{\"disc\":2,\"tracks\":[{\"track\":1,\"title\":\"b\","
        "\"audio\":{\"path\":\"audio/b1.mpc\",\"sha256\":\""
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}]}]}";
    musicpack_manifest *m = musicpack_manifest_parse(j, 0);
    CHECK(m != 0, "multi-disc parses");
    if (m == 0)
        return;
    CHECK(m->disc_count == 2, "two discs");
    CHECK(m->discs[0].disc == 1 && m->discs[1].disc == 2, "disc numbers");
    CHECK(m->discs[1].tracks[0].number == 1, "disc 2 track numbering restarts");
    musicpack_manifest_free(m);
}

static void
test_loudness_parse(void)
{
    const char *bad =
        "{\"format\":\"musicpack\",\"version\":1,"
        "\"album\":{\"title\":\"L\",\"artists\":[{\"name\":\"A\"}]},"
        "\"media\":[{\"disc\":1,\"tracks\":[{\"track\":1,\"title\":\"t\","
        "\"loudness\":{\"trackLUFS\":-5000,\"truePeakDbTP\":-0.5},"
        "\"audio\":{\"path\":\"audio/a.mpc\",\"sha256\":\""
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}]}]}";
    musicpack_manifest *m = musicpack_manifest_parse(bad, 0);
    CHECK(m == 0, "out-of-range loudness rejected");
    musicpack_manifest_free(m);

    CHECK(musicpack_loudness_validate_lufs(-10.6) == MUSICPACK_OK, "lufs -10.6 valid");
    CHECK(musicpack_loudness_validate_lufs(-9999) == MUSICPACK_ERR_INVALID, "lufs -9999 invalid");
    CHECK(musicpack_loudness_compute_gain(-10.0, -14.0) == -4.0, "gain derived");
}

/* ------------------------------------------------------------------ */
/* path security                                                       */
/* ------------------------------------------------------------------ */

static void
test_path_security(void)
{
    static const char *bad[] = {
        "../evil.mpc", "a/../../evil", "/etc/passwd", "a\\b.mpc",
        "audio/:x", "audio/\x01x", "", "a//b", "./a", "a/./b", "a/../b",
        "audio/", "..", ".", "a:", "C:/x",
    };
    static const char *good[] = {
        "audio/01 - Track.mpc", "artwork/front.jpg", "lyrics/01.lrc",
        "extras/notes.txt", "booklet/booklet.pdf", "a/b/c.mpc",
    };
    unsigned int i;

    for (i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK(musicpack_path_validate(bad[i]) == MUSICPACK_ERR_PATH,
              "bad path rejected");
    for (i = 0; i < sizeof good / sizeof *good; i++)
        CHECK(musicpack_path_validate(good[i]) == MUSICPACK_OK, "good path accepted");

    /* containment: resolve escapes rejected */
    {
        char root[512];
        char out[4096];
        if (make_temp_dir(root, sizeof root) == 0) {
            CHECK(musicpack_path_resolve(root, "../outside", out, sizeof out)
                  == MUSICPACK_ERR_PATH, "escape rejected");
            CHECK(musicpack_path_resolve(root, "audio/a.mpc", out, sizeof out)
                  == MUSICPACK_OK, "contained path resolves");
            CHECK(strncmp(out, root, strlen(root)) == 0, "resolved under root");
            remove_temp_dir(root, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* sha256                                                              */
/* ------------------------------------------------------------------ */

static void
test_sha256(void)
{
    char hex[MUSICPACK_SHA256_HEX_SIZE];
    static const char *abc = "abc";
    static const char *empty = "";

    musicpack_sha256(abc, 3, hex, sizeof hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223"
                      "b00361a396177a9cb410ff61f20015ad") == 0, "sha256(abc)");
    musicpack_sha256(empty, 0, hex, sizeof hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb924"
                      "27ae41e4649b934ca495991b7852b855") == 0, "sha256(empty)");
    CHECK(musicpack_sha256_eq(hex, "e3b0c44298fc1c149afbf4c8996fb924"
                                  "27ae41e4649b934ca495991b7852b855") == 1,
          "sha256 eq");
}

/* ------------------------------------------------------------------ */
/* BS.1770 meter                                                       */
/* ------------------------------------------------------------------ */

static void
test_meter(void)
{
    musicpack_meter *m;
    double lufs, peak;
    enum { RATE = 44100, FRAMES = RATE * 3 }; /* 3s for stable integration */
    float *buf = (float *) malloc(FRAMES * 2 * sizeof(float));
    int i;

    m = musicpack_meter_new(2, RATE, 0);
    CHECK(m != 0, "meter created");
    if (m == 0)
        return;

    /* full-scale 1 kHz sine in both channels -> stereo sum ~0 LUFS, ~0 dBTP */
    for (i = 0; i < FRAMES; i++) {
        float v = (float) sin(2.0 * M_PI * 1000.0 * i / RATE);
        buf[i * 2] = v;
        buf[i * 2 + 1] = v;
    }
    musicpack_meter_add_frames(m, buf, FRAMES);
    musicpack_meter_result(m, &lufs, &peak);
    CHECK(fabs(lufs) < 0.5, "full-scale stereo sine ~ 0 LUFS");
    CHECK(peak > -0.5 && peak < 0.5, "sine true peak ~ 0 dBTP");

    /* silence -> floor */
    musicpack_meter_free(m);
    m = musicpack_meter_new(2, RATE, 0);
    for (i = 0; i < RATE; i++) {
        buf[i * 2] = 0.0f;
        buf[i * 2 + 1] = 0.0f;
    }
    musicpack_meter_add_frames(m, buf, RATE);
    musicpack_meter_result(m, &lufs, &peak);
    CHECK(lufs <= -70.0, "silence floors at -70 LUFS");

    musicpack_meter_free(m);
    free(buf);
}

/* ------------------------------------------------------------------ */
/* release / edition model                                             */
/* ------------------------------------------------------------------ */

#define HASH_AAA "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

static const char *RELEASE_MANIFEST =
    "{"
    "  \"format\": \"musicpack\","
    "  \"version\": 1,"
    "  \"album\": {"
    "    \"title\": \"Discovery\","
    "    \"artists\": [ {\"name\": \"Daft Punk\", \"role\": \"main\"} ],"
    "    \"releaseType\": \"album\","
    "    \"originalReleaseDate\": \"2001-03-12\""
    "  },"
    "  \"release\": {"
    "    \"releaseDate\": \"2001-03-12\","
    "    \"edition\": \"2001 original release\","
    "    \"country\": \"Europe\","
    "    \"label\": \"Virgin\","
    "    \"catalogueNumber\": \"8496062\","
    "    \"notes\": \"Original mastering.\""
    "  },"
    "  \"identifiers\": {"
    "    \"musicbrainzReleaseGroupId\": \"rg-2001\","
    "    \"musicbrainzReleaseId\": \"rel-2001\","
    "    \"barcode\": \"724384960620\""
    "  },"
    "  \"media\": [ {"
    "    \"disc\": 1,"
    "    \"format\": \"CD\","
    "    \"tracks\": [ {"
    "      \"track\": 1,"
    "      \"title\": \"One More Time\","
    "      \"identifiers\": { \"isrc\": \"FRZ010100201\","
    "                          \"musicbrainzTrackId\": \"trk-2001\","
    "                          \"musicbrainzRecordingId\": \"rec-2001\" },"
    "      \"audio\": { \"path\": \"audio/01 - One More Time.mpc\", \"sha256\": \""
    HASH_AAA "\" }"
    "    } ]"
    "  } ]"
    "}";

/* The same album, but a different collectible release (1987 European CD). */
static const char *EDITION_MANIFEST =
    "{"
    "  \"format\": \"musicpack\","
    "  \"version\": 1,"
    "  \"album\": {"
    "    \"title\": \"Discovery\","
    "    \"artists\": [ {\"name\": \"Daft Punk\", \"role\": \"main\"} ],"
    "    \"releaseType\": \"album\","
    "    \"originalReleaseDate\": \"2001-03-12\""
    "  },"
    "  \"release\": {"
    "    \"releaseDate\": \"1987-11-02\","
    "    \"edition\": \"1987 European CD\","
    "    \"country\": \"DE\","
    "    \"label\": \"Carrere\","
    "    \"catalogueNumber\": \"CCS 1001\""
    "  },"
    "  \"identifiers\": {"
    "    \"musicbrainzReleaseGroupId\": \"rg-2001\","
    "    \"musicbrainzReleaseId\": \"rel-1987\""
    "  },"
    "  \"media\": [ {"
    "    \"disc\": 1,"
    "    \"format\": \"CD\","
    "    \"tracks\": [ {"
    "      \"track\": 1,"
    "      \"title\": \"One More Time\","
    "      \"audio\": { \"path\": \"audio/01 - One More Time.mpc\", \"sha256\": \""
    HASH_AAA "\" }"
    "    } ]"
    "  } ]"
    "}";

static void
test_release_model(void)
{
    musicpack_manifest *m;
    musicpack_status s;

    m = musicpack_manifest_parse(RELEASE_MANIFEST, &s);
    CHECK(m != 0, "release manifest parses");
    if (m == 0)
        return;
    CHECK(strcmp(m->release_type, "album") == 0, "release type");
    CHECK(strcmp(m->original_release_date, "2001-03-12") == 0, "original release date");
    CHECK(m->release.present == 1, "release present");
    CHECK(strcmp(m->release.release_date, "2001-03-12") == 0, "release date");
    CHECK(strcmp(m->release.edition, "2001 original release") == 0, "edition");
    CHECK(strcmp(m->release.country, "Europe") == 0, "country");
    CHECK(strcmp(m->release.label, "Virgin") == 0, "label");
    CHECK(strcmp(m->release.catalogue_number, "8496062") == 0, "catalogue number");
    CHECK(strcmp(m->release.notes, "Original mastering.") == 0, "release notes");
    CHECK(strcmp(m->musicbrainz_release_group_id, "rg-2001") == 0, "release group id");
    CHECK(strcmp(m->musicbrainz_release_id, "rel-2001") == 0, "release id");
    CHECK(strcmp(m->musicbrainz_release_group_id, m->musicbrainz_release_id) != 0,
          "release-group vs release identity distinct");
    CHECK(strcmp(m->barcode, "724384960620") == 0, "barcode");
    CHECK(m->disc_count == 1 && strcmp(m->discs[0].format, "CD") == 0, "medium format");
    CHECK(strcmp(m->discs[0].tracks[0].musicbrainz_track_id, "trk-2001") == 0,
          "track id");
    CHECK(strcmp(m->discs[0].tracks[0].musicbrainz_recording_id, "rec-2001") == 0,
          "recording id");
    musicpack_manifest_free(m);
}

static void
test_release_invalid_enum(void)
{
    musicpack_status s;
    char buf[2048];

    snprintf(buf, sizeof buf,
        "{\"format\":\"musicpack\",\"version\":1,"
        "\"album\":{\"title\":\"T\",\"artists\":[{\"name\":\"A\"}],"
        "\"releaseType\":\"mixtape\"},"
        "\"media\":[{\"disc\":1,\"tracks\":[{"
        "\"track\":1,\"title\":\"T\",\"audio\":{\"path\":\"a.mpc\",\"sha256\":\""
        HASH_AAA "\"}}]}]}");
    CHECK(musicpack_manifest_parse(buf, &s) == 0, "invalid release type rejected");

    snprintf(buf, sizeof buf,
        "{\"format\":\"musicpack\",\"version\":1,"
        "\"album\":{\"title\":\"T\",\"artists\":[{\"name\":\"A\"}]},"
        "\"media\":[{\"disc\":1,\"format\":\"DAT\",\"tracks\":[{"
        "\"track\":1,\"title\":\"T\",\"audio\":{\"path\":\"a.mpc\",\"sha256\":\""
        HASH_AAA "\"}}]}]}");
    CHECK(musicpack_manifest_parse(buf, &s) == 0, "invalid medium format rejected");

    snprintf(buf, sizeof buf,
        "{\"format\":\"musicpack\",\"version\":1,"
        "\"album\":{\"title\":\"T\",\"artists\":[{\"name\":\"A\"}]},"
        "\"media\":[{\"disc\":1,\"format\":\"Digital\",\"tracks\":[{"
        "\"track\":1,\"title\":\"T\",\"audio\":{\"path\":\"a.mpc\",\"sha256\":\""
        HASH_AAA "\"}}]}]}");
    CHECK(musicpack_manifest_parse(buf, &s) != 0, "digital single medium accepted");
}

static void
test_missing_release_optional(void)
{
    musicpack_manifest *m;
    musicpack_status s;

    m = musicpack_manifest_parse(VALID_MANIFEST, &s);
    CHECK(m != 0, "manifest without release parses");
    if (m == 0)
        return;
    CHECK(m->release.present == 0, "release not present");
    CHECK(m->release_type == 0, "release type absent");
    CHECK(m->original_release_date == 0, "original release date absent");
    CHECK(m->discs[0].format == 0, "medium format absent");
    musicpack_manifest_free(m);
}

static void
test_two_editions(void)
{
    musicpack_manifest *a, *b;
    musicpack_status s;

    a = musicpack_manifest_parse(RELEASE_MANIFEST, &s);
    b = musicpack_manifest_parse(EDITION_MANIFEST, &s);
    CHECK(a != 0 && b != 0, "two editions parse");
    if (a == 0 || b == 0)
        return;
    CHECK(strcmp(a->album_title, b->album_title) == 0, "same album");
    CHECK(strcmp(a->musicbrainz_release_group_id, b->musicbrainz_release_group_id) == 0,
          "same release group");
    CHECK(strcmp(a->release.edition, b->release.edition) != 0, "distinct editions");
    CHECK(strcmp(a->release.release_date, b->release.release_date) != 0, "distinct dates");
    CHECK(strcmp(a->musicbrainz_release_id, b->musicbrainz_release_id) != 0,
          "distinct specific-release IDs");
    CHECK(strcmp(a->release.label, b->release.label) != 0, "distinct labels");
    musicpack_manifest_free(a);
    musicpack_manifest_free(b);
}

/* ------------------------------------------------------------------ */
/* album loudness must be a program measurement, not an aggregation    */
/* ------------------------------------------------------------------ */

static void
fill_sine(float *buf, size_t frames, double amp)
{
    size_t i;
    for (i = 0; i < frames; i++) {
        float v = (float) (amp * sin(2.0 * M_PI * 1000.0 * (double) i / 44100.0));
        buf[i * 2] = v;
        buf[i * 2 + 1] = v;
    }
}

static void
test_album_loudness_aggregation(void)
{
    enum { RATE = 44100, TF = RATE * 3 }; /* 3s per track */
    float *a = (float *) malloc(TF * 2 * sizeof(float));
    float *b = (float *) malloc(TF * 2 * sizeof(float));
    float *concat = (float *) malloc(TF * 4 * sizeof(float));
    musicpack_meter *ma = 0, *mb = 0, *mab = 0, *mc = 0;
    double la = 0, lb = 0, pa = 0, pb = 0;
    double lab = 0, pab = 0, lc = 0, pc = 0;

    if (a == 0 || b == 0 || concat == 0) {
        CHECK(0, "alloc");
        return;
    }
    fill_sine(a, TF, 1.0);    /* full-scale -> ~0 LUFS, ~0 dBTP */
    fill_sine(b, TF, 0.25);   /* -12 dB -> ~-12 LUFS */
    memcpy(concat, a, TF * 2 * sizeof(float));
    memcpy(concat + TF * 2, b, TF * 2 * sizeof(float));

    ma = musicpack_meter_new(2, RATE, 0);
    musicpack_meter_add_frames(ma, a, TF);
    musicpack_meter_result(ma, &la, &pa);
    mb = musicpack_meter_new(2, RATE, 0);
    musicpack_meter_add_frames(mb, b, TF);
    musicpack_meter_result(mb, &lb, &pb);

    /* album meter: feed track A then track B into ONE meter */
    mab = musicpack_meter_new(2, RATE, 0);
    musicpack_meter_add_frames(mab, a, TF);
    musicpack_meter_add_frames(mab, b, TF);
    musicpack_meter_result(mab, &lab, &pab);

    /* reference: feed the concatenated program in one shot */
    mc = musicpack_meter_new(2, RATE, 0);
    musicpack_meter_add_frames(mc, concat, TF * 2);
    musicpack_meter_result(mc, &lc, &pc);

    CHECK(pa > pb, "track peaks differ (loud track louder)");
    CHECK(fabs(lab - lc) < 0.01,
          "album LUFS: sequential feeds == concatenated program");
    CHECK(fabs(lab - (la + lb) / 2.0) > 0.5,
          "album LUFS is NOT the arithmetic mean of track LUFS");
    {
        double mx = pa > pb ? pa : pb;
        CHECK(fabs(pab - mx) < 0.01,
              "album true peak == max of per-track true peaks");
    }
    CHECK(fabs(pab - pc) < 0.01, "album true peak identical across feed modes");

    musicpack_meter_free(ma);
    musicpack_meter_free(mb);
    musicpack_meter_free(mab);
    musicpack_meter_free(mc);
    free(a);
    free(b);
    free(concat);
}

/* ------------------------------------------------------------------ */
/* release metadata write round-trip                                   */
/* ------------------------------------------------------------------ */

static void
test_release_roundtrip(void)
{
    musicpack_manifest m;
    musicpack_manifest *back;
    char *json = 0;
    musicpack_status s;

    memset(&m, 0, sizeof m);
    m.album_title = strdup("RT");
    m.album_artists = (musicpack_artist *) calloc(1, sizeof *m.album_artists);
    m.album_artists[0].name = strdup("A");
    m.album_artist_count = 1;
    m.release_type = strdup("ep");
    m.original_release_date = strdup("1992-05-01");
    m.release.present = 1;
    m.release.release_date = strdup("1992-05-01");
    m.release.edition = strdup("1992 CD Maxi-Single");
    m.release.country = strdup("GB");
    m.release.label = strdup("Strike");
    m.release.catalogue_number = strdup("STRIKE 1");
    m.release.notes = strdup("12-track maxi-single.");
    m.musicbrainz_release_group_id = strdup("rg-rt");
    m.musicbrainz_release_id = strdup("rel-rt");
    m.barcode = strdup("1234567890128");
    m.discs = (musicpack_disc *) calloc(1, sizeof *m.discs);
    m.disc_count = 1;
    m.discs[0].disc = 1;
    m.discs[0].format = strdup("Digital");
    m.discs[0].tracks = (musicpack_track *) calloc(1, sizeof *m.discs[0].tracks);
    m.discs[0].track_count = 1;
    m.discs[0].tracks[0].number = 1;
    m.discs[0].tracks[0].title = strdup("T1");
    m.discs[0].tracks[0].isrc = strdup("GBXXX9200001");
    m.discs[0].tracks[0].musicbrainz_track_id = strdup("trk-rt");
    m.discs[0].tracks[0].musicbrainz_recording_id = strdup("rec-rt");
    m.discs[0].tracks[0].audio.path = strdup("audio/01 - T1.mpc");
    m.discs[0].tracks[0].audio.sha256 = strdup(HASH_AAA);

    CHECK(musicpack_manifest_write(&m, &json) == MUSICPACK_OK, "write release manifest");
    CHECK(json != 0 && strstr(json, "\"release\"") != 0, "release object written");
    CHECK(json != 0 && strstr(json, "\"releaseType\"") != 0, "release type written");
    CHECK(json != 0 && strstr(json, "\"originalReleaseDate\"") != 0, "original date written");
    CHECK(json != 0 && strstr(json, "\"musicbrainzReleaseGroupId\"") != 0, "release group written");

    back = musicpack_manifest_parse(json, &s);
    CHECK(back != 0, "written manifest re-parses");
    if (back != 0) {
        CHECK(strcmp(back->release.edition, "1992 CD Maxi-Single") == 0, "edition round-trips");
        CHECK(strcmp(back->discs[0].format, "Digital") == 0, "medium format round-trips");
        CHECK(strcmp(back->discs[0].tracks[0].musicbrainz_recording_id, "rec-rt") == 0,
              "recording id round-trips");
        CHECK(strcmp(back->release_type, "ep") == 0, "release type round-trips");
        musicpack_manifest_free(back);
    }

    musicpack_manifest_clear(&m);
    free(json);
}

/* ------------------------------------------------------------------ */
/* determinism                                                         */
/* ------------------------------------------------------------------ */

static void
test_determinism(void)
{
    musicpack_manifest m;
    char *j1 = 0, *j2 = 0;

    memset(&m, 0, sizeof m);
    m.album_title = strdup("D");
    m.album_artists = (musicpack_artist *) calloc(1, sizeof *m.album_artists);
    m.album_artists[0].name = strdup("Artist");
    m.album_artist_count = 1;
    m.discs = (musicpack_disc *) calloc(1, sizeof *m.discs);
    m.disc_count = 1;
    m.discs[0].disc = 1;
    m.discs[0].tracks = (musicpack_track *) calloc(1, sizeof *m.discs[0].tracks);
    m.discs[0].track_count = 1;
    m.discs[0].tracks[0].number = 1;
    m.discs[0].tracks[0].title = strdup("T");
    m.discs[0].tracks[0].audio.path =
        strdup("audio/01 - T.mpc");
    m.discs[0].tracks[0].audio.sha256 = strdup(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

    CHECK(musicpack_manifest_write(&m, &j1) == MUSICPACK_OK, "write 1");
    CHECK(musicpack_manifest_write(&m, &j2) == MUSICPACK_OK, "write 2");
    CHECK(j1 != 0 && j2 != 0 && strcmp(j1, j2) == 0, "deterministic output");

    musicpack_manifest_clear(&m);
    free(j1);
    free(j2);
}

/* ------------------------------------------------------------------ */
/* package open / verify / handoff on the reference packages           */
/* ------------------------------------------------------------------ */

static void
test_open_musicpack(const char *dir)
{
    musicpack_package *pkg = musicpack_package_open_dir(dir, 0);
    const musicpack_manifest *m;
    musicpack_report rep = { 0, 0 };

    CHECK(pkg != 0, "open mpc reference package");
    if (pkg == 0)
        return;
    m = musicpack_package_manifest(pkg);
    CHECK(strcmp(m->album_title, "Synthetic Test Compilation") == 0, "album title");
    CHECK(m->album_artist_count == 2, "multi-value artists");
    CHECK(m->disc_count == 1 && m->discs[0].track_count == 4, "4 tracks");
    CHECK(m->artwork_count == 1 && m->booklet_count == 1, "artwork + booklet");
    CHECK(m->lyrics_count == 2 && m->extras_count == 1, "lyrics + extras");
    CHECK(musicpack_package_verify(pkg, &rep, 0, 0) == MUSICPACK_OK, "verify ok");
    CHECK(rep.errors == 0, "no errors");

    /* Musepack handoff: decode track 1 through libmusepack */
    {
        mpc_reader reader;
        musepack_decoder *dec;
        float pcm[1152 * 2];
        uint64_t frames, total = 0;
        CHECK(musicpack_package_track_open_reader(pkg, 0, 0, &reader) == MUSICPACK_OK,
              "track reader");
        dec = musepack_decoder_open(&reader, 0);
        CHECK(dec != 0, "decoder over libmusicpack reader");
        if (dec != 0) {
            while (musepack_decoder_read(dec, pcm, 1152, &frames) == MUSEPACK_OK)
                total += frames;
            CHECK(total == 44100, "decoded 44100 frames via handoff");
            musepack_decoder_close(dec);
        }
        mpc_reader_exit_stdio(&reader);
    }
    musicpack_package_close(pkg);
}

static void
test_open_flac(const char *dir)
{
    musicpack_package *pkg = musicpack_package_open_dir(dir, 0);
    const musicpack_manifest *m;
    musicpack_report rep = { 0, 0 };

    CHECK(pkg != 0, "open flac reference package");
    if (pkg == 0)
        return;
    m = musicpack_package_manifest(pkg);
    CHECK(m->disc_count == 1 && m->discs[0].track_count == 3, "3 flac tracks");
    CHECK(strcmp(m->discs[0].tracks[0].audio.path + strlen(m->discs[0].tracks[0].audio.path) - 5,
                 ".flac") == 0, "flac codec independence");
    CHECK(musicpack_package_verify(pkg, &rep, 0, 0) == MUSICPACK_OK, "verify ok");
    musicpack_package_close(pkg);
}

/* ------------------------------------------------------------------ */
/* Phase 3A meta: tag-set model, Vorbis Comment, FLAC metadata reader  */
/* ------------------------------------------------------------------ */

static unsigned char *
build_vorbis_block(const char *vendor, const char *const *tags, size_t ntags,
                   size_t *outlen)
{
    size_t cap = 8 + strlen(vendor);
    size_t i, p = 0;
    unsigned char *buf;

    for (i = 0; i < ntags; i++)
        cap += 4 + strlen(tags[i]);
    buf = (unsigned char *) malloc(cap);
    if (buf == 0)
        return 0;
    {
        unsigned len = (unsigned) strlen(vendor);
        buf[p++] = (unsigned char) len;
        buf[p++] = (unsigned char) (len >> 8);
        buf[p++] = (unsigned char) (len >> 16);
        buf[p++] = (unsigned char) (len >> 24);
        memcpy(buf + p, vendor, len);
        p += len;
    }
    {
        unsigned n = (unsigned) ntags;
        buf[p++] = (unsigned char) n;
        buf[p++] = (unsigned char) (n >> 8);
        buf[p++] = (unsigned char) (n >> 16);
        buf[p++] = (unsigned char) (n >> 24);
    }
    for (i = 0; i < ntags; i++) {
        size_t clen = strlen(tags[i]);
        unsigned len = (unsigned) clen;
        buf[p++] = (unsigned char) len;
        buf[p++] = (unsigned char) (len >> 8);
        buf[p++] = (unsigned char) (len >> 16);
        buf[p++] = (unsigned char) (len >> 24);
        memcpy(buf + p, tags[i], clen);
        p += clen;
    }
    *outlen = p;
    return buf;
}

static void
test_utf8_valid(void)
{
    CHECK(musicpack_utf8_valid((const unsigned char *) "abc", 3) == 1, "ascii ok");
    {
        const unsigned char e[] = { 0xC3, 0xA9 }; /* é */
        CHECK(musicpack_utf8_valid(e, 2) == 1, "2-byte utf8 ok");
    }
    {
        const unsigned char bad[] = { 0xC0, 0xAF }; /* overlong */
        CHECK(musicpack_utf8_valid(bad, 2) == 0, "overlong rejected");
    }
    {
        const unsigned char bad[] = { 0xED, 0xA0, 0x80 }; /* surrogate */
        CHECK(musicpack_utf8_valid(bad, 3) == 0, "surrogate rejected");
    }
    {
        const unsigned char bad[] = { 0x80 }; /* continuation lead */
        CHECK(musicpack_utf8_valid(bad, 1) == 0, "continuation rejected");
    }
    {
        const unsigned char bad[] = { 0xF4, 0x90, 0x80, 0x80 }; /* > U+10FFFF */
        CHECK(musicpack_utf8_valid(bad, 4) == 0, "out-of-range rejected");
    }
}

static void
test_tag_set(void)
{
    musicpack_tag_set s;
    const musicpack_tag *t;
    const musicpack_tag *all[8];
    size_t n;
    unsigned char bin[] = { 0x89, 0x50, 0x4E, 0x47 };

    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_tag_set_add(&s, "TITLE", "Big in Japan", 12) == MUSICPACK_OK,
          "add text");
    CHECK(musicpack_tag_set_add(&s, "artist", "Alphaville", 10) == MUSICPACK_OK,
          "add lowercase key");
    CHECK(musicpack_tag_set_add(&s, "GENRE", "Synthpop", 8) == MUSICPACK_OK,
          "add genre");
    CHECK(musicpack_tag_set_add(&s, "GENRE", "New Wave", 8) == MUSICPACK_OK,
          "add repeated key");
    /* case-insensitive lookup, first match */
    t = musicpack_tag_set_get(&s, "title");
    CHECK(t != 0 && strcmp(t->value, "Big in Japan") == 0, "get case-insensitive");
    CHECK(musicpack_tag_set_get(&s, "MISSING") == 0, "get missing");
    n = musicpack_tag_set_get_all(&s, "genre", all, 8);
    CHECK(n == 2, "get_all returns both values");
    CHECK(strcmp(all[0]->value, "Synthpop") == 0, "get_all order 1");
    CHECK(strcmp(all[1]->value, "New Wave") == 0, "get_all order 2");

    /* binary item */
    CHECK(musicpack_tag_set_add_binary(&s, "Cover Art (Front)", bin, sizeof bin)
          == MUSICPACK_OK, "add binary");
    t = musicpack_tag_set_get(&s, "cover art (front)");
    CHECK(t != 0 && t->is_binary && t->binary_len == sizeof bin, "binary item");
    CHECK(t != 0 && t->value == 0 && memcmp(t->binary, bin, sizeof bin) == 0,
          "binary payload");

    /* embedded NUL truncation */
    CHECK(musicpack_tag_set_add(&s, "NULKEY", "abc\0def", 7) == MUSICPACK_OK,
          "nul value accepted");
    t = musicpack_tag_set_get(&s, "nulkey");
    CHECK(t != 0 && t->value_len == 3 && strcmp(t->value, "abc") == 0,
          "nul truncated");

    /* invalid inputs rejected */
    CHECK(musicpack_tag_set_add(&s, "", "x", 1) == MUSICPACK_ERR_INVALID,
          "empty key rejected");
    CHECK(musicpack_tag_set_add(&s, "BAD\nKEY", "x", 1) == MUSICPACK_ERR_INVALID,
          "control-char key rejected");
    {
        unsigned char inv[] = { 0xC3 }; /* truncated utf8 */
        CHECK(musicpack_tag_set_add(&s, "BADVAL", (const char *) inv, 1)
              == MUSICPACK_ERR_INVALID, "invalid utf8 value rejected");
    }
    CHECK(musicpack_tag_set_add(&s, "a", "x", (size_t) -1)
          == MUSICPACK_ERR_INVALID, "oversized value rejected");

    musicpack_tag_set_free(&s);
}

static void
test_vorbis_parse(void)
{
    static const char *tags[] = {
        "TITLE=The Van", "ARTIST=Bleachers", "ARTIST=Jack Antonoff",
        "TRACKNUMBER=2/12", "MUSICBRAINZ_TRACKID=legacy-recording-id",
        "EMPTY=", "NOEQUALS-bare-entry",
    };
    unsigned char *buf;
    size_t len;
    musicpack_tag_set s;
    const musicpack_tag *t;
    const musicpack_tag *all[8];
    size_t n;

    buf = build_vorbis_block("vendor-test", tags, 7, &len);
    CHECK(buf != 0, "build vorbis block");
    if (buf == 0)
        return;
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_vorbis_parse(buf, len, &s) == MUSICPACK_OK, "parse ok");
    CHECK(strcmp(musicpack_tag_set_get(&s, "TITLE")->value, "The Van") == 0,
          "title");
    n = musicpack_tag_set_get_all(&s, "ARTIST", all, 8);
    CHECK(n == 2, "two artists");
    CHECK(strcmp(musicpack_tag_set_get(&s, "TRACKNUMBER")->value, "2/12") == 0,
          "n/total");
    CHECK(strcmp(musicpack_tag_set_get(&s, "MUSICBRAINZ_TRACKID")->value,
                 "legacy-recording-id") == 0, "legacy MB track id");
    t = musicpack_tag_set_get(&s, "EMPTY");
    CHECK(t != 0 && t->value_len == 0, "empty value preserved");
    /* bare entries without '=' are skipped, not added */
    CHECK(musicpack_tag_set_get(&s, "NOEQUALS-bare-entry") == 0, "bare entry skipped");
    musicpack_tag_set_free(&s);

    /* truncation mid-field must fail, not over-read */
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init2");
    CHECK(musicpack_vorbis_parse(buf, len / 2, &s) == MUSICPACK_ERR_INVALID,
          "truncated block rejected");
    musicpack_tag_set_free(&s);
    free(buf);
}

static void
test_vorbis_read_file(const char *path)
{
    musicpack_tag_set s;

    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_vorbis_read(path, &s) == MUSICPACK_OK, "vorbis_read ok");
    CHECK(strcmp(musicpack_tag_set_get(&s, "TITLE")->value, "Big in Japan") == 0,
          "title from file");
    CHECK(strcmp(musicpack_tag_set_get(&s, "SOURCE")->value, "Deezer") == 0,
          "source from file");
    musicpack_tag_set_free(&s);
}

static void
test_flac_metadata(const char *dir)
{
    char path[1024];
    musicpack_tag_set c;
    musicpack_pictures p;
    const musicpack_tag *all[8];
    size_t n;

    snprintf(path, sizeof path, "%s/album-vorbis.flac", dir);
    CHECK(musicpack_tag_set_init(&c, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_flac_read_metadata(path, &c, &p) == MUSICPACK_OK, "flac read ok");
    CHECK(strcmp(musicpack_tag_set_get(&c, "TITLE")->value, "Big in Japan") == 0,
          "title");
    n = musicpack_tag_set_get_all(&c, "ARTIST", all, 8);
    CHECK(n == 2, "two artists");
    CHECK(strcmp(musicpack_tag_set_get(&c, "ALBUM")->value,
                 "Synthetic Test Album") == 0, "album");
    CHECK(strcmp(musicpack_tag_set_get(&c, "ALBUMARTIST")->value,
                 "Alphaville") == 0, "albumartist");
    CHECK(strcmp(musicpack_tag_set_get(&c, "TRACKNUMBER")->value, "3/12") == 0,
          "tracknumber n/total");
    CHECK(strcmp(musicpack_tag_set_get(&c, "DISCNUMBER")->value, "1/1") == 0,
          "discnumber n/total");
    CHECK(strcmp(musicpack_tag_set_get(&c, "DATE")->value, "2016-09-23") == 0,
          "date");
    CHECK(strcmp(musicpack_tag_set_get(&c, "ORIGINALDATE")->value,
                 "1984-06-01") == 0, "originaldate");
    n = musicpack_tag_set_get_all(&c, "GENRE", all, 8);
    CHECK(n == 2, "two genres");
    CHECK(strcmp(musicpack_tag_set_get(&c, "PUBLISHER")->value,
                 "Example Records") == 0, "publisher");
    CHECK(strcmp(musicpack_tag_set_get(&c, "CATALOGNUMBER")->value,
                 "ERCD 001") == 0, "catalog number");
    CHECK(strcmp(musicpack_tag_set_get(&c, "BARCODE")->value,
                 "198704979941") == 0, "barcode");
    CHECK(strcmp(musicpack_tag_set_get(&c, "ISRC")->value, "GBK3W2503556") == 0,
          "isrc");
    CHECK(strcmp(musicpack_tag_set_get(&c, "MUSICBRAINZ_ALBUMID")->value,
                 "11111111-2222-3333-4444-555555555555") == 0, "mb album id");
    CHECK(strcmp(musicpack_tag_set_get(&c, "MUSICBRAINZ_RELEASEGROUPID")->value,
                 "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee") == 0, "mb release group");
    CHECK(strcmp(musicpack_tag_set_get(&c, "MUSICBRAINZ_RECORDINGID")->value,
                 "12121212-3434-5656-7878-909090909090") == 0, "mb recording id");
    CHECK(strcmp(musicpack_tag_set_get(&c, "MUSICBRAINZ_RELEASETRACKID")->value,
                 "23232323-4545-6767-8989-abababababab") == 0, "mb release track");
    CHECK(strcmp(musicpack_tag_set_get(&c, "SOURCE")->value, "Deezer") == 0,
          "source");
    CHECK(strcmp(musicpack_tag_set_get(&c, "SOURCEID")->value, "3810015612") == 0,
          "source id");
    CHECK(strcmp(musicpack_tag_set_get(&c, "LYRICS")->value,
                 "Line one\nLine two") == 0, "lyrics multi-line");
    CHECK(strcmp(musicpack_tag_set_get(&c, "CUSTOM_X")->value, "survives") == 0,
          "custom tag preserved");
    CHECK(strcmp(musicpack_tag_set_get(&c, "title")->value, "Big in Japan") == 0,
          "case-insensitive get");

    CHECK(p.count == 2, "two pictures");
    if (p.count >= 1) {
        CHECK(p.items[0].type == 3, "front picture type");
        CHECK(p.items[0].mime != 0 && strcmp(p.items[0].mime, "image/png") == 0,
              "front mime");
        CHECK(p.items[0].width == 8 && p.items[0].height == 8, "front dims");
        CHECK(p.items[0].data_len > 0 && p.items[0].data[0] == 0x89,
              "png magic preserved");
    }
    if (p.count >= 2) {
        CHECK(p.items[1].type == 4, "back picture type");
        CHECK(p.items[1].mime != 0 && strcmp(p.items[1].mime, "image/jpeg") == 0,
              "back mime");
        CHECK(p.items[1].data_len > 0 && p.items[1].data[0] == 0xFF,
              "jpeg magic preserved");
    }
    musicpack_tag_set_free(&c);
    musicpack_pictures_free(&p);
}

static void
test_flac_negatives(const char *dir)
{
    char path[1024];
    musicpack_tag_set c;
    musicpack_pictures p;

    /* notags: valid FLAC, no comment/picture blocks */
    snprintf(path, sizeof path, "%s/notags.flac", dir);
    CHECK(musicpack_tag_set_init(&c, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_flac_read_metadata(path, &c, &p) == MUSICPACK_OK, "notags ok");
    CHECK(c.count == 0 && p.count == 0, "no tags no pictures");
    musicpack_tag_set_free(&c);
    musicpack_pictures_free(&p);

    /* bad magic */
    snprintf(path, sizeof path, "%s/bad-magic.flac", dir);
    CHECK(musicpack_tag_set_init(&c, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_flac_read_metadata(path, &c, &p) == MUSICPACK_ERR_INVALID,
          "bad magic rejected");
    musicpack_tag_set_free(&c);
    musicpack_pictures_free(&p);

    /* truncated inside the comment block */
    snprintf(path, sizeof path, "%s/truncated.flac", dir);
    CHECK(musicpack_tag_set_init(&c, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_flac_read_metadata(path, &c, &p) == MUSICPACK_ERR_INVALID,
          "truncated flac rejected");
    musicpack_tag_set_free(&c);
    musicpack_pictures_free(&p);

    /* block length past EOF */
    snprintf(path, sizeof path, "%s/oversized.flac", dir);
    CHECK(musicpack_tag_set_init(&c, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_flac_read_metadata(path, &c, &p) == MUSICPACK_ERR_INVALID,
          "oversized block rejected");
    musicpack_tag_set_free(&c);
    musicpack_pictures_free(&p);
}

static void
test_ape_read_fixture(const char *dir)
{
    char path[1024];
    musicpack_tag_set s;
    const musicpack_tag *all[8];
    const musicpack_tag *cov;
    size_t n;

    snprintf(path, sizeof path, "%s/album-ape.mpc", dir);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_ape_read(path, &s) == MUSICPACK_OK, "ape read ok");
    CHECK(strcmp(musicpack_tag_set_get(&s, "Title")->value, "Big in Japan") == 0,
          "title");
    n = musicpack_tag_set_get_all(&s, "Artist", all, 8);
    CHECK(n == 2, "multi-value artist split");
    CHECK(n == 2 && strcmp(all[0]->value, "Alphaville") == 0, "artist 1");
    CHECK(n == 2 && strcmp(all[1]->value, "The Van") == 0, "artist 2");
    CHECK(strcmp(musicpack_tag_set_get(&s, "Album")->value,
                 "Synthetic Test Album") == 0, "album");
    CHECK(strcmp(musicpack_tag_set_get(&s, "Track")->value, "3/12") == 0, "track");
    CHECK(strcmp(musicpack_tag_set_get(&s, "MusicBrainz Album Id")->value,
                 "11111111-2222-3333-4444-555555555555") == 0, "mb album id");
    CHECK(strcmp(musicpack_tag_set_get(&s, "Source")->value, "Deezer") == 0,
          "source");
    CHECK(strcmp(musicpack_tag_set_get(&s, "CUSTOM")->value, "survives") == 0,
          "custom preserved");
    cov = musicpack_tag_set_get(&s, "Cover Art (Front)");
    CHECK(cov != 0 && cov->is_binary, "cover art is binary");
    CHECK(cov != 0 && cov->binary_len >= 12, "cover art has payload");
    CHECK(cov != 0 && memcmp(cov->binary, "cover.jpg", 9) == 0 &&
          cov->binary[9] == '\0', "cover filename");
    CHECK(cov != 0 && cov->binary[10] == 0x89 && cov->binary[11] == 0x50,
          "png magic preserved");
    musicpack_tag_set_free(&s);

    snprintf(path, sizeof path, "%s/ape-no-tag.mpc", dir);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_ape_read(path, &s) == MUSICPACK_OK, "no-tag file ok");
    CHECK(s.count == 0, "no-tag file yields empty set");
    musicpack_tag_set_free(&s);

    snprintf(path, sizeof path, "%s/ape-truncated.mpc", dir);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_ape_read(path, &s) == MUSICPACK_ERR_INVALID,
          "truncated tag rejected");
    musicpack_tag_set_free(&s);
}

static void
test_ape_write(void)
{
    char dir[512];
    char file[600];
    FILE *f;
    static const char payload[] = "AUDIOBYTES";
    musicpack_tag_set s;
    const musicpack_tag *all[8];
    size_t n;

    if (make_temp_dir(dir, sizeof dir) != 0) {
        CHECK(0, "make temp dir");
        return;
    }
    snprintf(file, sizeof file, "%s/t.bin", dir);
    f = fopen(file, "wb");
    CHECK(f != 0, "write payload file");
    if (f != 0) {
        fwrite(payload, 1, strlen(payload), f);
        fclose(f);
    }

    /* write a tag, read it back */
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_tag_set_add(&s, "Title", "Big in Japan", 12) == MUSICPACK_OK,
          "add title");
    CHECK(musicpack_tag_set_add(&s, "Artist", "Alphaville", 10) == MUSICPACK_OK,
          "add artist1");
    CHECK(musicpack_tag_set_add(&s, "Artist", "The Van", 7) == MUSICPACK_OK,
          "add artist2");
    CHECK(musicpack_ape_write(file, &s) == MUSICPACK_OK, "write tag");
    musicpack_tag_set_free(&s);

    {
        char probe[64];
        size_t got = 0;
        f = fopen(file, "rb");
        CHECK(f != 0, "reopen");
        if (f != 0) {
            got = fread(probe, 1, strlen(payload), f);
            fclose(f);
        }
        CHECK(got == strlen(payload) && memcmp(probe, payload, strlen(payload)) == 0,
              "audio bytes preserved");
    }
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_ape_read(file, &s) == MUSICPACK_OK, "read back");
    CHECK(strcmp(musicpack_tag_set_get(&s, "Title")->value, "Big in Japan") == 0,
          "title round-trip");
    n = musicpack_tag_set_get_all(&s, "Artist", all, 8);
    CHECK(n == 2 && strcmp(all[0]->value, "Alphaville") == 0 &&
          strcmp(all[1]->value, "The Van") == 0, "multi-value round-trip");
    musicpack_tag_set_free(&s);

    /* replace with a different tag */
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_tag_set_add(&s, "Title", "Changed", 7) == MUSICPACK_OK, "add");
    CHECK(musicpack_ape_write(file, &s) == MUSICPACK_OK, "write replaced tag");
    musicpack_tag_set_free(&s);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_ape_read(file, &s) == MUSICPACK_OK, "read replaced");
    CHECK(s.count == 1, "old tags gone");
    CHECK(strcmp(musicpack_tag_set_get(&s, "Title")->value, "Changed") == 0,
          "replacement value");
    CHECK(musicpack_tag_set_get(&s, "Artist") == 0, "artist removed");
    musicpack_tag_set_free(&s);

    /* empty write removes the tag */
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_ape_write(file, &s) == MUSICPACK_OK, "write empty removes tag");
    musicpack_tag_set_free(&s);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(musicpack_ape_read(file, &s) == MUSICPACK_OK, "read after removal");
    CHECK(s.count == 0, "tag removed");
    musicpack_tag_set_free(&s);

    remove_temp_dir(dir, "t.bin");
}

/* ------------------------------------------------------------------ */
/* Phase 3A mapping core: tag-set -> manifest, manifest -> APEv2       */
/* ------------------------------------------------------------------ */

static musicpack_status
ts_add(musicpack_tag_set *s, const char *key, const char *value)
{
    return musicpack_tag_set_add(s, key, value, strlen(value));
}

static void
test_meta_helpers(void)
{
    int n = 0;
    CHECK(musicpack_meta_parse_track_number("3", &n) == 1 && n == 3, "track '3'");
    CHECK(musicpack_meta_parse_track_number("3/12", &n) == 1 && n == 3, "track '3/12'");
    CHECK(musicpack_meta_parse_track_number("03", &n) == 1 && n == 3, "track '03'");
    CHECK(musicpack_meta_parse_track_number("abc", &n) == 0, "track 'abc' rejected");
    CHECK(musicpack_meta_parse_track_number("0", &n) == 0, "track '0' rejected");
    CHECK(musicpack_meta_parse_track_number("", &n) == 0, "track '' rejected");

    CHECK(strcmp(musicpack_meta_release_type_from_tag("album"), "album") == 0,
          "type album");
    CHECK(strcmp(musicpack_meta_release_type_from_tag("single"), "single") == 0,
          "type single");
    CHECK(strcmp(musicpack_meta_release_type_from_tag("ep"), "ep") == 0, "type ep");
    CHECK(strcmp(musicpack_meta_release_type_from_tag("compilation"), "compilation") == 0,
          "type compilation");
    CHECK(strcmp(musicpack_meta_release_type_from_tag("soundtrack"), "soundtrack") == 0,
          "type soundtrack");
    CHECK(strcmp(musicpack_meta_release_type_from_tag("live"), "live-album") == 0,
          "type live -> live-album");
    CHECK(strcmp(musicpack_meta_release_type_from_tag("remix"), "remix-album") == 0,
          "type remix -> remix-album");
    CHECK(strcmp(musicpack_meta_release_type_from_tag("demo"), "other") == 0,
          "type demo -> other");
    CHECK(strcmp(musicpack_meta_release_type_from_tag("whatever"), "other") == 0,
          "type unknown -> other");
    CHECK(musicpack_meta_release_type_from_tag(0) == 0, "type NULL -> NULL");
    CHECK(musicpack_meta_release_type_from_tag("") == 0, "type empty -> NULL");
}

static void
test_map_album_vorbis(void)
{
    musicpack_tag_set s;
    musicpack_manifest m;

    memset(&m, 0, sizeof m);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(ts_add(&s, "ALBUM", "Discovery") == MUSICPACK_OK, "album");
    CHECK(ts_add(&s, "ALBUMARTIST", "Daft Punk") == MUSICPACK_OK, "albumartist");
    CHECK(ts_add(&s, "ORIGINALDATE", "2001-03-12") == MUSICPACK_OK, "origdate");
    CHECK(ts_add(&s, "DATE", "2016-09-23") == MUSICPACK_OK, "date");
    CHECK(ts_add(&s, "GENRE", "Electronic") == MUSICPACK_OK, "genre1");
    CHECK(ts_add(&s, "GENRE", "House") == MUSICPACK_OK, "genre2");
    CHECK(ts_add(&s, "PUBLISHER", "Virgin") == MUSICPACK_OK, "publisher");
    CHECK(ts_add(&s, "CATALOGNUMBER", "8496062") == MUSICPACK_OK, "catalogue");
    CHECK(ts_add(&s, "BARCODE", "724384960620") == MUSICPACK_OK, "barcode");
    CHECK(ts_add(&s, "MUSICBRAINZ_ALBUMID", "rel-2001") == MUSICPACK_OK, "mb relid");
    CHECK(ts_add(&s, "MUSICBRAINZ_RELEASEGROUPID", "rg-2001") == MUSICPACK_OK, "mb rgid");
    CHECK(ts_add(&s, "MUSICBRAINZ_ALBUMTYPE", "album") == MUSICPACK_OK, "mb type");
    CHECK(ts_add(&s, "MUSICBRAINZ_ALBUMCOUNTRY", "XE") == MUSICPACK_OK, "mb country");
    CHECK(ts_add(&s, "SOURCE", "Deezer") == MUSICPACK_OK, "source");
    CHECK(ts_add(&s, "SOURCEID", "3810015612") == MUSICPACK_OK, "sourceid");

    CHECK(musicpack_tag_map_album(&s, &m) == MUSICPACK_OK, "map album");
    CHECK(m.album_title != 0 && strcmp(m.album_title, "Discovery") == 0, "title");
    CHECK(m.album_artist_count == 1, "one album artist");
    CHECK(m.album_artist_count == 1 && strcmp(m.album_artists[0].name, "Daft Punk") == 0
          && strcmp(m.album_artists[0].role, "main") == 0, "album artist + role");
    CHECK(m.release_type != 0 && strcmp(m.release_type, "album") == 0, "release type");
    CHECK(m.original_release_date != 0 &&
          strcmp(m.original_release_date, "2001-03-12") == 0, "original date");
    CHECK(m.genre_count == 2, "two genres");
    CHECK(m.genre_count == 2 && strcmp(m.genres[0], "Electronic") == 0 &&
          strcmp(m.genres[1], "House") == 0, "genre values");
    CHECK(m.release.present == 1, "release present");
    CHECK(m.release.release_date != 0 &&
          strcmp(m.release.release_date, "2016-09-23") == 0, "release date");
    CHECK(m.release.country != 0 && strcmp(m.release.country, "XE") == 0, "country");
    CHECK(m.release.label != 0 && strcmp(m.release.label, "Virgin") == 0, "label");
    CHECK(m.release.catalogue_number != 0 &&
          strcmp(m.release.catalogue_number, "8496062") == 0, "catalogue");
    CHECK(m.barcode != 0 && strcmp(m.barcode, "724384960620") == 0, "barcode");
    CHECK(m.musicbrainz_release_id != 0 &&
          strcmp(m.musicbrainz_release_id, "rel-2001") == 0, "mb release id");
    CHECK(m.musicbrainz_release_group_id != 0 &&
          strcmp(m.musicbrainz_release_group_id, "rg-2001") == 0, "mb release group");
    CHECK(m.source_store != 0 && strcmp(m.source_store, "Deezer") == 0,
          "source store");
    CHECK(m.source_type != 0 && strcmp(m.source_type, "digital-download") == 0,
          "source type");
    CHECK(m.source_id != 0 && strcmp(m.source_id, "3810015612") == 0, "source id");

    /* identity is never set by tag mapping */
    CHECK(m.identity_source == 0 && m.identity_confidence == 0, "identity untouched");

    musicpack_manifest_clear(&m);
    musicpack_tag_set_free(&s);
}

static void
test_map_album_ape(void)
{
    musicpack_tag_set s;
    musicpack_manifest m;

    memset(&m, 0, sizeof m);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(ts_add(&s, "Album", "Discovery") == MUSICPACK_OK, "album");
    CHECK(ts_add(&s, "Album Artist", "Daft Punk") == MUSICPACK_OK, "album artist");
    CHECK(ts_add(&s, "Year", "2016") == MUSICPACK_OK, "year");
    CHECK(ts_add(&s, "Label", "Virgin") == MUSICPACK_OK, "label");
    CHECK(ts_add(&s, "CatalogNumber", "8496062") == MUSICPACK_OK, "catalog");
    CHECK(ts_add(&s, "MusicBrainz Album Id", "rel-2001") == MUSICPACK_OK, "mb id");
    CHECK(ts_add(&s, "MusicBrainz Release Group Id", "rg-2001") == MUSICPACK_OK, "mb rg");
    CHECK(ts_add(&s, "MusicBrainz Album Type", "live") == MUSICPACK_OK, "mb type");
    CHECK(ts_add(&s, "MusicBrainz Album Country", "DE") == MUSICPACK_OK, "mb country");

    CHECK(musicpack_tag_map_album(&s, &m) == MUSICPACK_OK, "map album ape");
    CHECK(m.album_title != 0 && strcmp(m.album_title, "Discovery") == 0, "title");
    CHECK(m.release_type != 0 && strcmp(m.release_type, "live-album") == 0,
          "live -> live-album");
    CHECK(m.release.release_date != 0 && strcmp(m.release.release_date, "2016") == 0,
          "year -> release date");
    CHECK(m.release.country != 0 && strcmp(m.release.country, "DE") == 0, "country");
    CHECK(m.release.label != 0 && strcmp(m.release.label, "Virgin") == 0, "label");
    CHECK(m.release.catalogue_number != 0 &&
          strcmp(m.release.catalogue_number, "8496062") == 0, "catalogue");
    CHECK(m.musicbrainz_release_id != 0 &&
          strcmp(m.musicbrainz_release_id, "rel-2001") == 0, "mb release id");
    CHECK(m.musicbrainz_release_group_id != 0 &&
          strcmp(m.musicbrainz_release_group_id, "rg-2001") == 0, "mb release group");
    CHECK(m.album_artist_count == 1 && strcmp(m.album_artists[0].name, "Daft Punk") == 0,
          "album artist");

    musicpack_manifest_clear(&m);
    musicpack_tag_set_free(&s);
}

static void
test_map_album_first_wins(void)
{
    musicpack_tag_set s;
    musicpack_manifest m;

    memset(&m, 0, sizeof m);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(ts_add(&s, "ALBUM", "First Album") == MUSICPACK_OK, "album1");
    CHECK(ts_add(&s, "ALBUMARTIST", "Artist One") == MUSICPACK_OK, "aa1");
    CHECK(musicpack_tag_map_album(&s, &m) == MUSICPACK_OK, "map 1");

    CHECK(ts_add(&s, "ALBUM", "Second Album") == MUSICPACK_OK, "album2");
    CHECK(ts_add(&s, "ALBUMARTIST", "Artist Two") == MUSICPACK_OK, "aa2");
    CHECK(ts_add(&s, "GENRE", "Jazz") == MUSICPACK_OK, "genre");
    CHECK(musicpack_tag_map_album(&s, &m) == MUSICPACK_OK, "map 2");

    CHECK(m.album_title != 0 && strcmp(m.album_title, "First Album") == 0,
          "album title first-wins");
    CHECK(m.album_artist_count == 1 && strcmp(m.album_artists[0].name, "Artist One") == 0,
          "album artist first-wins");
    CHECK(m.genre_count == 1 && strcmp(m.genres[0], "Jazz") == 0, "genre appended once");

    musicpack_manifest_clear(&m);
    musicpack_tag_set_free(&s);
}

static void
test_map_track(void)
{
    musicpack_tag_set s;
    musicpack_track t;

    memset(&t, 0, sizeof t);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(ts_add(&s, "TITLE", "Big in Japan") == MUSICPACK_OK, "title");
    CHECK(ts_add(&s, "TRACKNUMBER", "3/12") == MUSICPACK_OK, "tracknum");
    CHECK(ts_add(&s, "ARTIST", "Alphaville") == MUSICPACK_OK, "artist");
    CHECK(ts_add(&s, "COMPOSER", "Bernhard Lloyd") == MUSICPACK_OK, "composer");
    CHECK(ts_add(&s, "ISRC", "GBK3W2503556") == MUSICPACK_OK, "isrc");
    CHECK(ts_add(&s, "MUSICBRAINZ_RECORDINGID", "rec-2001") == MUSICPACK_OK, "mb rec");
    CHECK(ts_add(&s, "MUSICBRAINZ_RELEASETRACKID", "trk-2001") == MUSICPACK_OK, "mb trk");
    CHECK(ts_add(&s, "SOURCE", "Deezer") == MUSICPACK_OK, "source");
    CHECK(ts_add(&s, "SOURCEID", "3810015612") == MUSICPACK_OK, "sourceid");

    CHECK(musicpack_tag_map_track(&s, &t) == MUSICPACK_OK, "map track");
    CHECK(t.title != 0 && strcmp(t.title, "Big in Japan") == 0, "title");
    CHECK(t.number == 3, "track number 3");
    CHECK(t.artist_count == 2, "two artists");
    CHECK(t.artist_count == 2 && strcmp(t.artists[0].name, "Alphaville") == 0
          && strcmp(t.artists[0].role, "main") == 0, "main artist");
    CHECK(t.artist_count == 2 && strcmp(t.artists[1].name, "Bernhard Lloyd") == 0
          && strcmp(t.artists[1].role, "composer") == 0, "composer role");
    CHECK(t.isrc != 0 && strcmp(t.isrc, "GBK3W2503556") == 0, "isrc");
    CHECK(t.musicbrainz_recording_id != 0 &&
          strcmp(t.musicbrainz_recording_id, "rec-2001") == 0, "recording id");
    CHECK(t.musicbrainz_track_id != 0 &&
          strcmp(t.musicbrainz_track_id, "trk-2001") == 0, "track id");
    CHECK(t.source_store != 0 && strcmp(t.source_store, "Deezer") == 0, "source store");
    CHECK(t.source_track_id != 0 && strcmp(t.source_track_id, "3810015612") == 0,
          "source track id");
    free(t.title);
    free(t.isrc);
    free(t.musicbrainz_recording_id);
    free(t.musicbrainz_track_id);
    free(t.source_store);
    free(t.source_track_id);
    for (size_t i = 0; i < t.artist_count; i++) {
        free(t.artists[i].name);
        free(t.artists[i].role);
    }
    free(t.artists);
    musicpack_tag_set_free(&s);
}

static void
test_map_track_legacy_and_ape(void)
{
    musicpack_tag_set s;
    musicpack_track t;

    /* legacy MUSICBRAINZ_TRACKID -> recording id fallback */
    memset(&t, 0, sizeof t);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(ts_add(&s, "MUSICBRAINZ_TRACKID", "legacy-rec") == MUSICPACK_OK, "legacy");
    CHECK(musicpack_tag_map_track(&s, &t) == MUSICPACK_OK, "map");
    CHECK(t.musicbrainz_recording_id != 0 &&
          strcmp(t.musicbrainz_recording_id, "legacy-rec") == 0,
          "legacy trackid -> recording id");
    musicpack_tag_set_free(&s);
    free(t.musicbrainz_recording_id);

    /* APE title-cased keys */
    memset(&t, 0, sizeof t);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(ts_add(&s, "Title", "The Van") == MUSICPACK_OK, "title");
    CHECK(ts_add(&s, "Track", "5/12") == MUSICPACK_OK, "track");
    CHECK(ts_add(&s, "Artist", "Bleachers") == MUSICPACK_OK, "artist");
    CHECK(ts_add(&s, "ISRC", "GBK3W2503556") == MUSICPACK_OK, "isrc");
    CHECK(ts_add(&s, "MusicBrainz Recording Id", "rec-2001") == MUSICPACK_OK, "rec");
    CHECK(ts_add(&s, "MusicBrainz Release Track Id", "trk-2001") == MUSICPACK_OK, "trk");
    CHECK(musicpack_tag_map_track(&s, &t) == MUSICPACK_OK, "map ape");
    CHECK(t.number == 5, "ape track number 5");
    CHECK(t.title != 0 && strcmp(t.title, "The Van") == 0, "ape title");
    CHECK(t.musicbrainz_recording_id != 0 &&
          strcmp(t.musicbrainz_recording_id, "rec-2001") == 0, "ape recording id");
    CHECK(t.musicbrainz_track_id != 0 &&
          strcmp(t.musicbrainz_track_id, "trk-2001") == 0, "ape track id");
    musicpack_tag_set_free(&s);
    free(t.title);
    free(t.isrc);
    free(t.musicbrainz_recording_id);
    free(t.musicbrainz_track_id);
    free(t.artists);
}

static void
test_map_source(void)
{
    musicpack_tag_set s;
    musicpack_manifest m;

    memset(&m, 0, sizeof m);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(ts_add(&s, "SOURCE", "cd-rip") == MUSICPACK_OK, "source");
    CHECK(musicpack_tag_map_album(&s, &m) == MUSICPACK_OK, "map");
    CHECK(m.source_type != 0 && strcmp(m.source_type, "cd-rip") == 0, "cd-rip type");
    CHECK(m.source_store == 0, "no store for cd-rip");
    musicpack_manifest_clear(&m);
    musicpack_tag_set_free(&s);

    memset(&m, 0, sizeof m);
    CHECK(musicpack_tag_set_init(&s, "test") == MUSICPACK_OK, "init");
    CHECK(ts_add(&s, "SOURCE", "CD Rip") == MUSICPACK_OK, "source");
    CHECK(musicpack_tag_map_album(&s, &m) == MUSICPACK_OK, "map");
    CHECK(m.source_type != 0 && strcmp(m.source_type, "cd-rip") == 0, "cd rip type");
    musicpack_manifest_clear(&m);
    musicpack_tag_set_free(&s);
}

static void
test_projection(void)
{
    musicpack_manifest m;
    musicpack_track t;
    musicpack_tag_set s;
    musicpack_manifest m2;
    musicpack_track t2;
    const char *v;

    memset(&m, 0, sizeof m);
    memset(&t, 0, sizeof t);
    m.album_title = strdup("Discovery");
    m.album_artists = (musicpack_artist *) calloc(1, sizeof *m.album_artists);
    m.album_artists[0].name = strdup("Daft Punk");
    m.album_artists[0].role = strdup("main");
    m.album_artist_count = 1;
    m.release_type = strdup("album");
    m.original_release_date = strdup("2001-03-12");
    m.genres = (char **) calloc(1, sizeof *m.genres);
    m.genres[0] = strdup("Electronic");
    m.genre_count = 1;
    m.release.present = 1;
    m.release.release_date = strdup("2016-09-23");
    m.release.country = strdup("XE");
    m.release.label = strdup("Virgin");
    m.release.catalogue_number = strdup("8496062");
    m.barcode = strdup("724384960620");
    m.musicbrainz_release_group_id = strdup("rg-2001");
    m.musicbrainz_release_id = strdup("rel-2001");
    m.source_store = strdup("Deezer");
    m.source_type = strdup("digital-download");
    m.source_id = strdup("album-source-id");

    t.number = 3;
    t.title = strdup("Big in Japan");
    t.artists = (musicpack_artist *) calloc(1, sizeof *t.artists);
    t.artists[0].name = strdup("Alphaville");
    t.artists[0].role = strdup("main");
    t.artist_count = 1;
    t.isrc = strdup("GBK3W2503556");
    t.musicbrainz_recording_id = strdup("rec-2001");
    t.musicbrainz_track_id = strdup("trk-2001");
    t.source_store = strdup("Deezer");
    t.source_track_id = strdup("3810015612");

    CHECK(musicpack_manifest_to_ape_tags(&m, &t, 1, 2, 12, &s) == MUSICPACK_OK,
          "project to ape");
    v = musicpack_tag_set_get(&s, "Album") ? musicpack_tag_set_get(&s, "Album")->value : 0;
    CHECK(v != 0 && strcmp(v, "Discovery") == 0, "project album");
    v = musicpack_tag_set_get(&s, "Track") ? musicpack_tag_set_get(&s, "Track")->value : 0;
    CHECK(v != 0 && strcmp(v, "3/12") == 0, "project track n/total");
    v = musicpack_tag_set_get(&s, "Disc") ? musicpack_tag_set_get(&s, "Disc")->value : 0;
    CHECK(v != 0 && strcmp(v, "1/2") == 0, "project disc n/total");
    v = musicpack_tag_set_get(&s, "MusicBrainz Recording Id")
            ? musicpack_tag_set_get(&s, "MusicBrainz Recording Id")->value : 0;
    CHECK(v != 0 && strcmp(v, "rec-2001") == 0, "project recording id");
    v = musicpack_tag_set_get(&s, "MusicBrainz Album Type")
            ? musicpack_tag_set_get(&s, "MusicBrainz Album Type")->value : 0;
    CHECK(v != 0 && strcmp(v, "album") == 0, "project release type");
    v = musicpack_tag_set_get(&s, "SourceId")
            ? musicpack_tag_set_get(&s, "SourceId")->value : 0;
    CHECK(v != 0 && strcmp(v, "3810015612") == 0, "project track source id");

    /* map the projection back and compare */
    memset(&m2, 0, sizeof m2);
    memset(&t2, 0, sizeof t2);
    CHECK(musicpack_tag_map_album(&s, &m2) == MUSICPACK_OK, "map back album");
    CHECK(musicpack_tag_map_track(&s, &t2) == MUSICPACK_OK, "map back track");
    CHECK(m2.album_title != 0 && strcmp(m2.album_title, "Discovery") == 0,
          "round-trip album");
    CHECK(m2.album_artist_count == 1 && strcmp(m2.album_artists[0].name, "Daft Punk") == 0,
          "round-trip album artist");
    CHECK(m2.release_type != 0 && strcmp(m2.release_type, "album") == 0,
          "round-trip release type");
    CHECK(m2.original_release_date != 0 &&
          strcmp(m2.original_release_date, "2001-03-12") == 0, "round-trip orig date");
    CHECK(m2.release.release_date != 0 && strcmp(m2.release.release_date, "2016-09-23") == 0,
          "round-trip release date");
    CHECK(m2.release.label != 0 && strcmp(m2.release.label, "Virgin") == 0,
          "round-trip label");
    CHECK(m2.release.catalogue_number != 0 &&
          strcmp(m2.release.catalogue_number, "8496062") == 0, "round-trip catalogue");
    CHECK(m2.barcode != 0 && strcmp(m2.barcode, "724384960620") == 0,
          "round-trip barcode");
    CHECK(m2.musicbrainz_release_group_id != 0 &&
          strcmp(m2.musicbrainz_release_group_id, "rg-2001") == 0, "round-trip rgid");
    CHECK(m2.musicbrainz_release_id != 0 &&
          strcmp(m2.musicbrainz_release_id, "rel-2001") == 0, "round-trip relid");
    CHECK(m2.source_store != 0 && strcmp(m2.source_store, "Deezer") == 0,
          "round-trip source store");
    CHECK(t2.title != 0 && strcmp(t2.title, "Big in Japan") == 0, "round-trip title");
    CHECK(t2.number == 3, "round-trip track number");
    CHECK(t2.isrc != 0 && strcmp(t2.isrc, "GBK3W2503556") == 0, "round-trip isrc");
    CHECK(t2.musicbrainz_recording_id != 0 &&
          strcmp(t2.musicbrainz_recording_id, "rec-2001") == 0, "round-trip recording");
    CHECK(t2.musicbrainz_track_id != 0 &&
          strcmp(t2.musicbrainz_track_id, "trk-2001") == 0, "round-trip track id");

    musicpack_manifest_clear(&m2);
    free(t2.title);
    free(t2.isrc);
    free(t2.musicbrainz_recording_id);
    free(t2.musicbrainz_track_id);
    free(t2.source_store);
    free(t2.source_track_id);
    for (size_t i = 0; i < t2.artist_count; i++) {
        free(t2.artists[i].name);
        free(t2.artists[i].role);
    }
    free(t2.artists);
    musicpack_tag_set_free(&s);
    musicpack_manifest_clear(&m);
    free(t.title);
    free(t.isrc);
    free(t.musicbrainz_recording_id);
    free(t.musicbrainz_track_id);
    free(t.source_store);
    free(t.source_track_id);
    for (size_t i = 0; i < t.artist_count; i++) {
        free(t.artists[i].name);
        free(t.artists[i].role);
    }
    free(t.artists);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *metadir = 0;

    if (argc >= 3 && strcmp(argv[1], "--parse-meta") == 0) {
        /* fuzz mode: parse a FLAC file; a crash is a signal exit (>=128) */
        musicpack_tag_set c;
        musicpack_pictures p;
        musicpack_tag_set_init(&c, "fuzz");
        musicpack_flac_read_metadata(argv[2], &c, &p);
        musicpack_tag_set_free(&c);
        musicpack_pictures_free(&p);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "--parse-ape") == 0) {
        /* fuzz mode: parse an APEv2 tag; a crash is a signal exit (>=128) */
        musicpack_tag_set s;
        musicpack_tag_set_init(&s, "fuzz");
        musicpack_ape_read(argv[2], &s);
        musicpack_tag_set_free(&s);
        return 0;
    }
    if (argc < 3) {
        fprintf(stderr, "usage: %s <mpc-album.mpack> <flac-album.mpack> [meta-dir]\n",
                argv[0]);
        return 2;
    }
    if (argc >= 4)
        metadir = argv[3];
    test_parse_valid();
    test_parse_invalid();
    test_unknown_field_roundtrip();
    test_multidisc();
    test_loudness_parse();
    test_release_model();
    test_release_invalid_enum();
    test_missing_release_optional();
    test_two_editions();
    test_album_loudness_aggregation();
    test_release_roundtrip();
    test_path_security();
    test_sha256();
    test_meter();
    test_determinism();
    test_open_musicpack(argv[1]);
    test_open_flac(argv[2]);

    if (metadir != 0) {
        char vorbis_path[1024];
        test_utf8_valid();
        test_tag_set();
        test_vorbis_parse();
        snprintf(vorbis_path, sizeof vorbis_path, "%s/vorbis-comment.bin", metadir);
        test_vorbis_read_file(vorbis_path);
        test_flac_metadata(metadir);
        test_flac_negatives(metadir);
        test_ape_read_fixture(metadir);
        test_ape_write();
        test_meta_helpers();
        test_map_album_vorbis();
        test_map_album_ape();
        test_map_album_first_wins();
        test_map_track();
        test_map_track_legacy_and_ape();
        test_map_source();
        test_projection();
    }

    if (failures) {
        fprintf(stderr, "%d mpack test(s) failed\n", failures);
        return 1;
    }
    printf("all mpack tests passed\n");
    return 0;
}
