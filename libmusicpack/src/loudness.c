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
/// \file loudness.c
/// BS.1770-4 loudness measurement (via vendored libebur128) and helpers.

#include <float.h>
#include <math.h>
#include <stdlib.h>

#include "ebur128.h"

#include <musicpack/loudness.h>

/* Integrated loudness floors: libebur128 reports -HUGE_VAL for silence. */
#define MUSICPACK_LOUDNESS_FLOOR (-70.0)

struct musicpack_meter {
    ebur128_state *st;
    unsigned channels;
    double running_true_peak_lin; /* max linear true peak over all calls */
};

musicpack_meter *
musicpack_meter_new(unsigned channels, unsigned sample_rate, musicpack_status *status)
{
    musicpack_meter *m;
    unsigned i;

    if (status != 0)
        *status = MUSICPACK_OK;
    if (channels == 0 || channels > 2 || sample_rate == 0) {
        if (status != 0)
            *status = MUSICPACK_ERR_INVALID;
        return 0;
    }

    m = (musicpack_meter *) calloc(1, sizeof *m);
    if (m == 0) {
        if (status != 0)
            *status = MUSICPACK_ERR_NOMEM;
        return 0;
    }
    m->channels = channels;
    m->st = ebur128_init(channels, (unsigned long) sample_rate,
                         EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK);
    if (m->st == 0) {
        free(m);
        if (status != 0)
            *status = MUSICPACK_ERR_NOMEM;
        return 0;
    }
    for (i = 0; i < channels; i++)
        ebur128_set_channel(m->st, i, i == 0 ? EBUR128_LEFT : EBUR128_RIGHT);
    return m;
}

void
musicpack_meter_free(musicpack_meter *m)
{
    if (m == 0)
        return;
    ebur128_destroy(&m->st);
    free(m);
}

musicpack_status
musicpack_meter_add_frames(musicpack_meter *m, const float *interleaved, size_t frames)
{
    unsigned i;

    if (m == 0 || (interleaved == 0 && frames != 0))
        return MUSICPACK_ERR_INVALID;
    if (frames == 0)
        return MUSICPACK_OK;
    if (ebur128_add_frames_float(m->st, interleaved, frames) != EBUR128_SUCCESS)
        return MUSICPACK_ERR_NOMEM;

    for (i = 0; i < m->channels; i++) {
        double peak = 0.0;
        if (ebur128_true_peak(m->st, i, &peak) == EBUR128_SUCCESS && peak > m->running_true_peak_lin)
            m->running_true_peak_lin = peak;
    }
    return MUSICPACK_OK;
}

musicpack_status
musicpack_meter_result(const musicpack_meter *m, double *integrated_lufs, double *true_peak_db)
{
    double lufs;

    if (m == 0 || integrated_lufs == 0 || true_peak_db == 0)
        return MUSICPACK_ERR_INVALID;
    if (ebur128_loudness_global(m->st, &lufs) != EBUR128_SUCCESS)
        return MUSICPACK_ERR_INVALID;

    if (!isfinite(lufs) || lufs < MUSICPACK_LOUDNESS_FLOOR)
        lufs = MUSICPACK_LOUDNESS_FLOOR;
    *integrated_lufs = lufs;

    if (m->running_true_peak_lin <= 0.0)
        *true_peak_db = MUSICPACK_LOUDNESS_FLOOR;
    else {
        double db = 20.0 * log10(m->running_true_peak_lin);
        *true_peak_db = db < MUSICPACK_LOUDNESS_FLOOR ? MUSICPACK_LOUDNESS_FLOOR : db;
    }
    return MUSICPACK_OK;
}

double
musicpack_loudness_compute_gain(double measured_lufs, double target_lufs)
{
    return target_lufs - measured_lufs;
}

musicpack_status
musicpack_loudness_validate_lufs(double lufs)
{
    if (!isfinite(lufs) || lufs < -70.0 || lufs > 6.0)
        return MUSICPACK_ERR_INVALID;
    return MUSICPACK_OK;
}

musicpack_status
musicpack_loudness_validate_true_peak(double db)
{
    if (!isfinite(db) || db < -70.0 || db > 6.0)
        return MUSICPACK_ERR_INVALID;
    return MUSICPACK_OK;
}
