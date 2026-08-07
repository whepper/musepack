/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see codec.h)
*/
#include "codec.h"
#include "mime.h"

#include <stdio.h>
#include <string.h>

#include <musepack/musepack.h>

/* ---- FLAC STREAMINFO: first metadata block after the "fLaC" magic. ---- */
static int
flac_probe(const char *path, mp_codec_info *out)
{
    unsigned char h[42];
    FILE *f = fopen(path, "rb");
    int rate = 0, ch = 0;

    if (f == 0)
        return MUSICPACK_ERR_IO;
    if (fread(h, 1, sizeof h, f) != sizeof h ||
        memcmp(h, "fLaC", 4) != 0) {
        fclose(f);
        return MUSICPACK_ERR_INVALID;
    }
    /* metadata block header: 1 byte (flag+type) + 3 bytes length */
    if ((h[4] & 0x7f) != 0) { /* 0 == STREAMINFO */
        fclose(f);
        return MUSICPACK_ERR_INVALID;
    }
    {
        const unsigned char *si = h + 8; /* STREAMINFO starts at offset 8 */
        rate = (si[10] << 12) | (si[11] << 4) | (si[12] >> 4);
        ch = ((si[12] & 0x0e) >> 1) + 1;
    }
    fclose(f);
    snprintf(out->codec, sizeof out->codec, "flac");
    out->stream_version = 0;
    out->sample_rate = rate;
    out->channels = ch;
    return MUSICPACK_OK;
}

musicpack_status
mp_codec_probe(const char *abs_path, const char *rel_path, mp_codec_info *out)
{
    memset(out, 0, sizeof *out);
    if (strcmp(mp_codec_for_path(rel_path), "musepack") == 0) {
        mpc_reader reader;
        musepack_decoder *dec;
        musepack_stream_info si;

        if (mpc_reader_init_stdio(&reader, abs_path) != MPC_STATUS_OK)
            return MUSICPACK_ERR_IO;
        dec = musepack_decoder_open(&reader, 0);
        if (dec == 0) {
            mpc_reader_exit_stdio(&reader);
            snprintf(out->codec, sizeof out->codec, "musepack");
            return MUSICPACK_OK;
        }
        memset(&si, 0, sizeof si);
        si.size = sizeof si;
        if (musepack_decoder_get_stream_info(dec, &si) == 0) {
            snprintf(out->codec, sizeof out->codec, "musepack-sv%d",
                     (int) si.stream_version);
            out->stream_version = (int) si.stream_version;
            out->sample_rate = (long long) si.sample_rate;
            out->channels = (long long) si.channels;
        } else {
            snprintf(out->codec, sizeof out->codec, "musepack");
        }
        musepack_decoder_close(dec);
        mpc_reader_exit_stdio(&reader);
        return MUSICPACK_OK;
    }
    if (strcmp(mp_codec_for_path(rel_path), "flac") == 0) {
        if (flac_probe(abs_path, out) == MUSICPACK_OK)
            return MUSICPACK_OK;
        snprintf(out->codec, sizeof out->codec, "flac");
        return MUSICPACK_OK;
    }
    snprintf(out->codec, sizeof out->codec, "%s", mp_codec_for_path(rel_path));
    return MUSICPACK_OK;
}
