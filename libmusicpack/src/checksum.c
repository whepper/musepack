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
/// \file checksum.c
/// SHA-256 (FIPS 180-4) used for `.mpack` integrity.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <musicpack/checksum.h>

/* ------------------------------------------------------------------ */
/* SHA-256 core                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned int h[8];
    unsigned long long len;
    unsigned char block[64];
    unsigned int block_used;
} mpc_sha256_ctx;

static const unsigned int SHA256_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static unsigned int
rotr32(unsigned int x, unsigned int n)
{
    return (x >> n) | (x << (32 - n));
}

static void
sha256_transform(mpc_sha256_ctx *c, const unsigned char *block)
{
    unsigned int w[64];
    unsigned int a, b, d, e, f, g, h, c2;
    unsigned int t1, t2;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = ((unsigned int) block[i * 4] << 24)
             | ((unsigned int) block[i * 4 + 1] << 16)
             | ((unsigned int) block[i * 4 + 2] << 8)
             | ((unsigned int) block[i * 4 + 3]);
    for (i = 16; i < 64; i++) {
        unsigned int s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        unsigned int s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = c->h[0]; b = c->h[1]; c2 = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];

    for (i = 0; i < 64; i++) {
        unsigned int S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        unsigned int ch = (e & f) ^ (~e & g);
        unsigned int S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        unsigned int maj = (a & b) ^ (a & c2) ^ (b & c2);
        t1 = h + S1 + ch + SHA256_K[i] + w[i];
        t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c2; c2 = b; b = a; a = t1 + t2;
    }

    c->h[0] += a; c->h[1] += b; c->h[2] += c2; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void
sha256_init(mpc_sha256_ctx *c)
{
    c->h[0] = 0x6a09e667u; c->h[1] = 0xbb67ae85u;
    c->h[2] = 0x3c6ef372u; c->h[3] = 0xa54ff53au;
    c->h[4] = 0x510e527fu; c->h[5] = 0x9b05688cu;
    c->h[6] = 0x1f83d9abu; c->h[7] = 0x5be0cd19u;
    c->len = 0;
    c->block_used = 0;
}

static void
sha256_update(mpc_sha256_ctx *c, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *) data;
    c->len += (unsigned long long) len;
    while (len > 0) {
        size_t take = 64 - c->block_used;
        if (take > len) take = len;
        memcpy(c->block + c->block_used, p, take);
        c->block_used += (unsigned int) take;
        p += take;
        len -= take;
        if (c->block_used == 64) {
            sha256_transform(c, c->block);
            c->block_used = 0;
        }
    }
}

static void
sha256_final(mpc_sha256_ctx *c, unsigned char out[32])
{
    unsigned long long bitlen = c->len * 8;
    unsigned char pad = 0x80;
    unsigned char zero = 0;
    unsigned char lenbuf[8];
    int i;

    /* length in big-endian 64-bit */
    for (i = 0; i < 8; i++)
        lenbuf[i] = (unsigned char) (bitlen >> (56 - i * 8));

    sha256_update(c, &pad, 1);
    while (c->block_used != 56)
        sha256_update(c, &zero, 1);
    sha256_update(c, lenbuf, 8);

    for (i = 0; i < 8; i++) {
        out[i * 4]     = (unsigned char) (c->h[i] >> 24);
        out[i * 4 + 1] = (unsigned char) (c->h[i] >> 16);
        out[i * 4 + 2] = (unsigned char) (c->h[i] >> 8);
        out[i * 4 + 3] = (unsigned char) c->h[i];
    }
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

musicpack_status
musicpack_sha256(const void *data, size_t len, char *hex, size_t cap)
{
    mpc_sha256_ctx c;
    unsigned char digest[32];
    static const char hexc[] = "0123456789abcdef";
    size_t i;

    if (hex == 0 || cap < MUSICPACK_SHA256_HEX_SIZE)
        return MUSICPACK_ERR_INVALID;
    sha256_init(&c);
    sha256_update(&c, data, len);
    sha256_final(&c, digest);
    for (i = 0; i < 32; i++) {
        hex[i * 2] = hexc[digest[i] >> 4];
        hex[i * 2 + 1] = hexc[digest[i] & 0xF];
    }
    hex[64] = '\0';
    return MUSICPACK_OK;
}

musicpack_status
musicpack_sha256_file(const char *path, char *hex, size_t cap)
{
    mpc_sha256_ctx c;
    unsigned char digest[32];
    unsigned char buf[65536];
    static const char hexc[] = "0123456789abcdef";
    FILE *f;
    size_t n, i;

    if (path == 0 || hex == 0 || cap < MUSICPACK_SHA256_HEX_SIZE)
        return MUSICPACK_ERR_INVALID;
    f = fopen(path, "rb");
    if (f == 0)
        return MUSICPACK_ERR_IO;
    sha256_init(&c);
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        sha256_update(&c, buf, n);
    if (ferror(f)) {
        fclose(f);
        return MUSICPACK_ERR_IO;
    }
    fclose(f);
    sha256_final(&c, digest);
    for (i = 0; i < 32; i++) {
        hex[i * 2] = hexc[digest[i] >> 4];
        hex[i * 2 + 1] = hexc[digest[i] & 0xF];
    }
    hex[64] = '\0';
    return MUSICPACK_OK;
}

int
musicpack_sha256_eq(const char *a, const char *b)
{
    size_t i, n;
    if (a == 0 || b == 0)
        return 0;
    n = strlen(a);
    if (n != strlen(b))
        return 0;
    /* constant-time style comparison */
    {
        unsigned char diff = 0;
        for (i = 0; i < n; i++)
            diff |= (unsigned char) a[i] ^ (unsigned char) b[i];
        return diff == 0;
    }
}
