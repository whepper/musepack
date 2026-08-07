/*
 * Unit tests for the Musepack decoder/encoder libraries.
 *
 * These test internal invariants directly (crc32, bitstream round-trip,
 * size/block parsing, Cnk table consistency) and are wired into CTest.
 *
 * Build: via tests/CMakeLists.txt (MPC_BUILD_TESTS=ON).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mpc/mpcdec.h>
#include "../libmpcdec/internal.h"
#include "../libmpcdec/huffman.h"
#include "../libmpcdec/mpc_bits_reader.h"
#include "../libmpcenc/libmpcenc.h"

unsigned long mpc_crc32(unsigned char *buf, int len);

/* sv7 quantizer LUTs, defined in huffman.c */
extern const mpc_lut_data mpc_HuffQ[7][2];

static int failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);    \
            failures++;                                                      \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------ */
/* crc32 against well-known vectors                                     */
/* ------------------------------------------------------------------ */
static void test_crc32(void)
{
    unsigned char empty[] = "";
    unsigned char hello[] = "123456789";

    /* Standard CRC-32 (IEEE) checksum values. */
    CHECK(mpc_crc32(empty, 0) == 0x00000000UL, "crc32('') == 0x00000000");
    CHECK(mpc_crc32(hello, 9) == 0xCBF43926UL, "crc32('123456789') == 0xCBF43926");

    /* Longer buffer: "The quick brown fox jumps over the lazy dog". */
    {
        const char *fox = "The quick brown fox jumps over the lazy dog";
        CHECK(mpc_crc32((unsigned char *) fox, (int) strlen(fox)) == 0x414FA339UL,
              "crc32(fox) == 0x414FA339");
    }
}

/* ------------------------------------------------------------------ */
/* Bit writer <-> reader round-trip for byte-aligned fields            */
/* ------------------------------------------------------------------ */
static void test_bits_roundtrip(void)
{
    mpc_encoder_t enc;
    mpc_uint8_t buffer[256];
    mpc_bits_reader r;

    memset(&enc, 0, sizeof enc);
    enc.buffer = buffer;
    enc.pos = 0;
    enc.bitsCount = 0;
    enc.outputBits = 0;

    /* The writeBits/read pairing is byte-aligned by design. */
    writeBits(&enc, 0x12, 8);
    writeBits(&enc, 0x3456, 16);
    writeBits(&enc, 0x789ABC, 24);
    writeBits(&enc, 0x0, 8);
    emptyBits(&enc);

    r.buff = buffer;
    r.count = 8;

    CHECK(mpc_bits_read(&r, 8) == 0x12, "bits read 8");
    CHECK(mpc_bits_read(&r, 16) == 0x3456, "bits read 16");
    CHECK(mpc_bits_read(&r, 24) == 0x789ABC, "bits read 24");
    CHECK(mpc_bits_read(&r, 8) == 0x0, "bits read 8 (padding)");
}

/* ------------------------------------------------------------------ */
/* size encoding/decoding round-trip                                   */
/* ------------------------------------------------------------------ */
static void test_size_roundtrip(void)
{
    mpc_uint64_t values[] = { 0, 1, 127, 128, 255, 16383, 16384, 0xFFFFFFFF,
                              0x7FFFFFFFFFFFFFFFULL };
    unsigned int j;

    for (j = 0; j < sizeof values / sizeof *values; j++) {
        mpc_uint8_t tmp[10];
        unsigned int len = encodeSize(values[j], (char *) tmp, MPC_FALSE);
        mpc_bits_reader r;
        mpc_uint64_t out = 0;
        (void) len;
        r.buff = tmp;
        r.count = 8;
        mpc_bits_get_size(&r, &out);
        CHECK(out == values[j], "size round-trip");
    }
}

/* ------------------------------------------------------------------ */
/* Cnk table consistency: Cnk[k][n] = C(n, k+1) ("n choose k+1")       */
/* ------------------------------------------------------------------ */
static void test_cnk_tables(void)
{
    int k, n;

    /* Pascal's rule: C(n, k+1) = C(n-1, k+1) + C(n-1, k). */
    for (k = 1; k < 16; k++)
        for (n = k + 1; n < 31; n++)
            CHECK(Cnk[k][n] == Cnk[k][n - 1] + Cnk[k - 1][n - 1],
                  "Pascal's rule on Cnk rows");

    /* C(16, 8) == 12870 (row k=7, column n=16). */
    CHECK(Cnk[7][16] == 12870, "C(16,8) == 12870");

    /* Length table monotonic growth: Cnk_len[k][n] <= Cnk_len[k][n+1]. */
    for (k = 0; k < 16; k++)
        for (n = 0; n < 31; n++)
            CHECK(Cnk_len[k][n] <= Cnk_len[k][n + 1], "Cnk_len monotonic");
}

/* ------------------------------------------------------------------ */
/* Huffman LUT construction: after init, decodable entries are valid   */
/* ------------------------------------------------------------------ */
static void test_huffman_luts(void)
{
    int i, j;

    huff_init_lut(LUT_DEPTH);

    /* For each sv7 quantizer LUT, every entry either resolves directly
       (Length != 0) or points at a table index (< 63 for Q7, etc.). */
    for (i = 0; i < 7; i++)
        for (j = 0; j < 2; j++) {
            int k;
            for (k = 0; k < (1 << LUT_DEPTH); k++) {
                mpc_huff_lut lut = mpc_HuffQ[i][j].lut[k];
                if (lut.Length == 0) {
                    /* unresolved: the stored value is a table index */
                    CHECK((unsigned char) lut.Value < 64, "LUT unresolved index in range");
                } else {
                    CHECK(lut.Length <= 16, "LUT resolved length <= 16");
                }
            }
        }
}

int main(void)
{
    test_crc32();
    test_bits_roundtrip();
    test_size_roundtrip();
    test_cnk_tables();
    test_huffman_luts();

    if (failures) {
        fprintf(stderr, "%d unit test(s) failed\n", failures);
        return 1;
    }
    printf("all unit tests passed\n");
    return 0;
}
