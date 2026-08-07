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
/// \file streaminfo.h
/// libmusepack stream information.
///
/// Two structures are available:
///
///  - \ref musepack_stream_info — the modern, versioned structure owned by
///    this API. New code and bindings should use it.
///  - `mpc_streaminfo` — the historical structure, re-exported from
///    <mpc/streaminfo.h> for compatibility. It is returned by the legacy
///    musepack_decoder_get_info().
#ifndef MUSEPACK_STREAMINFO_H_
#define MUSEPACK_STREAMINFO_H_
#pragma once

#include <stdint.h>
#include <mpc/streaminfo.h>

#include <musepack/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Minimum valid value for musepack_stream_info::size. This is the v1
/// structure size and never shrinks; future versions only append fields.
#define MUSEPACK_STREAM_INFO_MIN_SIZE ((unsigned int) sizeof(musepack_stream_info))

/// Versioned stream properties structure.
///
/// Callers must set `size` to `sizeof(musepack_stream_info)` (or at least
/// MUSEPACK_STREAM_INFO_MIN_SIZE) before calling
/// musepack_decoder_get_stream_info(). The library fills at most `size`
/// bytes, so consumers compiled against an older (smaller) layout keep
/// working with a newer library; new fields are always appended.
///
/// All integer fields use fixed-width types, so the layout is identical on
/// 32-bit and 64-bit platforms and imports cleanly into Swift/JNI.
typedef struct musepack_stream_info {
    mpc_uint32_t size;           ///< IN: sizeof(musepack_stream_info)
    mpc_uint32_t stream_version; ///< Stream version (7 or 8)
    mpc_uint32_t sample_rate;    ///< Sample rate in Hz
    mpc_uint32_t channels;       ///< Channel count (1 or 2)
    mpc_uint64_t length_samples; ///< Playable frames, excluding leading silence
    mpc_uint64_t total_samples;  ///< Encoded frames, including beg_silence
    mpc_uint64_t beg_silence;    ///< Leading silence in frames
    mpc_uint32_t max_band;       ///< Highest subband index used (0..31)
    mpc_uint32_t ms;             ///< 1 if mid/side stereo is used
    mpc_uint32_t block_pwr;      ///< SV8: frames per block = 2^block_pwr (SV7: 0)
    mpc_uint32_t is_true_gapless;///< 1 if the stream is gapless
    mpc_uint16_t gain_title;     ///< ReplayGain title gain, dB * 256
    mpc_uint16_t gain_album;     ///< ReplayGain album gain, dB * 256
    mpc_uint16_t peak_title;     ///< ReplayGain title peak, dB * 256
    mpc_uint16_t peak_album;     ///< ReplayGain album peak, dB * 256
    mpc_uint32_t encoder_version;
    char encoder[64];            ///< Human-readable encoder identification
    char profile_name[32];       ///< Quality profile name (fixed buffer, no ptr)
} musepack_stream_info;

#ifdef __cplusplus
}
#endif
#endif /* MUSEPACK_STREAMINFO_H_ */
