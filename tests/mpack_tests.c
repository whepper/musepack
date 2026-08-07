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
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <mpc-album.mpack> <flac-album.mpack>\n", argv[0]);
        return 2;
    }
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

    if (failures) {
        fprintf(stderr, "%d mpack test(s) failed\n", failures);
        return 1;
    }
    printf("all mpack tests passed\n");
    return 0;
}
