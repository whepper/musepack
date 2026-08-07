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
/// \file loudness.h
/// BS.1770-only loudness model and measurement.
///
/// The `.mpack` canonical loudness metadata is measured with ITU-R BS.1770-4
/// only. ReplayGain values read from `.mpc` are import-time compatibility
/// data, never canonical MusicPack metadata. Gain is derived, not stored:
/// see musicpack_loudness_compute_gain().
#ifndef MUSICPACK_LOUDNESS_H_
#define MUSICPACK_LOUDNESS_H_
#pragma once

#include <stddef.h>

#include <musicpack/error.h>
#include <musicpack/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque BS.1770-4 meter (K-weighted, integrated loudness, true peak).
typedef struct musicpack_meter musicpack_meter;

/// Creates a meter for a stream.
///
/// \param channels     channel count (1 or 2)
/// \param sample_rate  sample rate in Hz
/// \param status       optional error out
MUSICPACK_API musicpack_meter *musicpack_meter_new(unsigned channels,
                                                   unsigned sample_rate,
                                                   musicpack_status *status);

/// Releases a meter.
MUSICPACK_API void musicpack_meter_free(musicpack_meter *m);

/// Feeds interleaved float PCM frames into the meter.
///
/// \param m           meter
/// \param interleaved frames in L,R,L,R order
/// \param frames      number of sample-frames
MUSICPACK_API musicpack_status musicpack_meter_add_frames(musicpack_meter *m,
                                                          const float *interleaved,
                                                          size_t frames);

/// Returns the integrated loudness (dB LUFS) and true peak (dBTP) of the fed
/// signal. Negative true peaks (below -70 dB) may be reported as -70.
MUSICPACK_API musicpack_status musicpack_meter_result(const musicpack_meter *m,
                                                      double *integrated_lufs,
                                                      double *true_peak_db);

/// Derives the playback gain (dB) for a target integrated loudness.
///
///     gain_db = target_lufs - measured_lufs
///
/// No target is hard-coded by the library; the caller supplies the playback
/// policy.
MUSICPACK_API double musicpack_loudness_compute_gain(double measured_lufs,
                                                     double target_lufs);

/// Validates plausibility of a measured loudness value (finite, within
/// [-70, 0] LUFS). Returns MUSICPACK_OK if valid.
MUSICPACK_API musicpack_status musicpack_loudness_validate_lufs(double lufs);

/// Validates plausibility of a true-peak value in dBTP (finite, <= +6).
MUSICPACK_API musicpack_status musicpack_loudness_validate_true_peak(double db);

#ifdef __cplusplus
}
#endif
#endif /* MUSICPACK_LOUDNESS_H_ */
