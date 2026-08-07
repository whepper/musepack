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
/// \file musepack_decoder.c
/// Implementation of the stable libmusepack decoder-facing API.
///
/// This is a thin session facade over the existing opaque mpc_demux
/// interface. No codec logic lives here; it only formalizes lifecycle,
/// buffered PCM reading, seeking and stream checking for consumers.

#include <stdlib.h>
#include <string.h>
#include <mpc/mpcdec.h>
#include <musepack/musepack.h>

struct musepack_decoder {
    mpc_demux *demux;
    mpc_streaminfo si;
    uint64_t position;      ///< sample-frames returned by read since open/seek
    float *frame_buffer;    ///< one decoded interleaved frame (1152 * channels)
    uint64_t frame_filled;  ///< sample-frames already consumed from frame_buffer
    uint64_t frame_samples; ///< sample-frames (per channel) in frame_buffer
};

static musepack_error
mpc_status_to_error(mpc_status s)
{
    return s == MPC_STATUS_OK ? MUSEPACK_OK : MUSEPACK_ERR_INVALID;
}

static musepack_error
decode_frame(musepack_decoder *d)
{
    mpc_frame_info frame;
    mpc_status s;

    frame.buffer = d->frame_buffer;
    s = mpc_demux_decode(d->demux, &frame);
    if (s != MPC_STATUS_OK)
        return mpc_status_to_error(s);
    if (frame.bits == -1)
        return MUSEPACK_ERR_EOF;
    if (frame.samples == 0)
        return MUSEPACK_ERR_INVALID;
    d->frame_samples = frame.samples;
    d->frame_filled = 0;
    return MUSEPACK_OK;
}

musepack_decoder *
musepack_decoder_open(mpc_reader *reader, musepack_error *error_out)
{
    musepack_decoder *d;
    mpc_demux *demux;

    if (error_out != 0)
        *error_out = MUSEPACK_OK;
    if (reader == 0) {
        if (error_out != 0)
            *error_out = MUSEPACK_ERR_INVALID;
        return 0;
    }

    demux = mpc_demux_init(reader);
    if (demux == 0) {
        if (error_out != 0)
            *error_out = MUSEPACK_ERR_INVALID;
        return 0;
    }

    d = calloc(1, sizeof *d);
    if (d == 0) {
        mpc_demux_exit(demux);
        if (error_out != 0)
            *error_out = MUSEPACK_ERR_NOMEM;
        return 0;
    }
    mpc_demux_get_info(demux, &d->si);
    d->demux = demux;
    d->frame_buffer = malloc(sizeof(float) * (size_t) MPC_FRAME_LENGTH * d->si.channels);
    if (d->frame_buffer == 0) {
        mpc_demux_exit(demux);
        free(d);
        if (error_out != 0)
            *error_out = MUSEPACK_ERR_NOMEM;
        return 0;
    }
    return d;
}

void
musepack_decoder_close(musepack_decoder *d)
{
    if (d == 0)
        return;
    mpc_demux_exit(d->demux);
    free(d->frame_buffer);
    free(d);
}

musepack_error
musepack_decoder_get_info(const musepack_decoder *d, mpc_streaminfo *out)
{
    if (d == 0 || out == 0)
        return MUSEPACK_ERR_INVALID;
    memcpy(out, &d->si, sizeof d->si);
    return MUSEPACK_OK;
}

musepack_error
musepack_decoder_read(musepack_decoder *d, float *pcm, uint64_t max_frames,
                      uint64_t *frames_out)
{
    uint64_t written = 0, channels;

    if (frames_out != 0)
        *frames_out = 0;
    if (d == 0 || pcm == 0 || max_frames == 0)
        return MUSEPACK_ERR_INVALID;

    channels = d->si.channels;
    while (written < max_frames) {
        while (d->frame_filled >= d->frame_samples) {
            musepack_error e = decode_frame(d);
            if (e == MUSEPACK_ERR_EOF) {
                if (frames_out != 0)
                    *frames_out = written;
                return written != 0 ? MUSEPACK_OK : MUSEPACK_ERR_EOF;
            }
            if (e != MUSEPACK_OK)
                return e;
        }
        {
            uint64_t avail = d->frame_samples - d->frame_filled;
            uint64_t n = avail < (max_frames - written) ? avail : (max_frames - written);
            memcpy(pcm + written * channels, d->frame_buffer + d->frame_filled * channels,
                   (size_t)(n * channels) * sizeof(float));
            d->frame_filled += n;
            written += n;
        }
    }
    d->position += written;
    if (frames_out != 0)
        *frames_out = written;
    return MUSEPACK_OK;
}

musepack_error
musepack_decoder_seek_sample(musepack_decoder *d, uint64_t sample)
{
    uint64_t length;

    if (d == 0)
        return MUSEPACK_ERR_INVALID;
    length = musepack_decoder_length_samples(d);
    if (sample > length)
        sample = length;
    if (mpc_demux_seek_sample(d->demux, sample) != MPC_STATUS_OK)
        return MUSEPACK_ERR_SEEK;
    d->position = sample;
    d->frame_filled = 0;
    d->frame_samples = 0;
    return MUSEPACK_OK;
}

musepack_error
musepack_decoder_seek_seconds(musepack_decoder *d, double seconds)
{
    if (d == 0 || seconds < 0)
        return MUSEPACK_ERR_INVALID;
    return musepack_decoder_seek_sample(d,
            (uint64_t)(seconds * (double) d->si.sample_freq + 0.5));
}

uint64_t
musepack_decoder_position(const musepack_decoder *d)
{
    return d == 0 ? 0 : d->position;
}

uint64_t
musepack_decoder_length_samples(const musepack_decoder *d)
{
    if (d == 0)
        return 0;
    return (uint64_t) mpc_streaminfo_get_length_samples(&d->si);
}

musepack_error
musepack_decoder_check_stream(musepack_decoder *d)
{
    mpc_frame_info frame;

    if (d == 0)
        return MUSEPACK_ERR_INVALID;
    frame.buffer = d->frame_buffer;
    for (;;) {
        mpc_demux_set_samples_to_skip(d->demux,
                MPC_FRAME_LENGTH + MPC_DECODER_SYNTH_DELAY);
        if (mpc_demux_decode(d->demux, &frame) != MPC_STATUS_OK)
            return MUSEPACK_ERR_INVALID;
        if (frame.bits == -1)
            return MUSEPACK_OK;
    }
}
