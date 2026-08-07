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
/// \file decoder.h
/// Stable decoder-facing API for libmusepack.
///
/// This is the canonical C interface that the WebAssembly wrapper, future
/// Swift (iOS) and JNI (Android) wrappers, and all MusicPack components
/// consume. It is intentionally small, opaque and single-threaded.
///
/// # Lifecycle
///
///     mpc_reader            reader;                 // caller-owned
///     mpc_reader_init_stdio(&reader, "song.mpc");
///
///     musepack_error err;
///     musepack_decoder *d = musepack_decoder_open(&reader, &err);
///     if (!d) { ... }
///
///     mpc_streaminfo info;
///     musepack_decoder_get_info(d, &info);
///
///     float pcm[MUSEPACK_FRAME_MAX * 2];
///     uint64_t n;
///     while (musepack_decoder_read(d, pcm, MUSEPACK_FRAME_MAX, &n) == MUSEPACK_OK) {
///         // interleaved float PCM, n sample-frames
///     }
///
///     musepack_decoder_close(d);
///     mpc_reader_exit_stdio(&reader);
///
/// # Ownership rules
///
///  - The caller owns the reader. The decoder borrows it for its lifetime
///    and never frees it (or its `data`). Keep the reader alive until after
///    musepack_decoder_close().
///  - musepack_decoder_open() parses the stream header immediately and
///    fails (returning NULL) on unreadable or invalid input.
///  - create/open/decode/seek/close may be repeated any number of times;
///    each decoder is fully independent. Calling close() twice on the same
///    decoder, or using a decoder after close(), is undefined behaviour.
///  - Separate decoder instances (and their readers) are independent and
///    may be used concurrently from different threads. A single instance is
///    not thread-safe and must not be shared without external locking.
///
/// # Error handling
///
/// Every call that can fail reports a \ref musepack_error. Errors are
/// negative; MUSEPACK_OK (0) means success. Read and seek failures do not
/// invalidate the decoder: the error is reported once and the call may be
/// retried.
///
/// # Sample format
///
/// Decoded PCM is interleaved single-precision float in the range ~[-1, 1],
/// one sample per channel per frame (L, R, L, R, ... for stereo).
#ifndef MUSEPACK_DECODER_H_
#define MUSEPACK_DECODER_H_
#pragma once

#include <stdint.h>
#include <mpc/reader.h>
#include <mpc/streaminfo.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Maximum samples per channel in a single decoded frame (36 subbands * 32).
#define MUSEPACK_FRAME_MAX 1152

/// Opaque decoder session handle. Allocated by musepack_decoder_open().
typedef struct musepack_decoder musepack_decoder;

/// Predictable error codes. Success is 0; all errors are negative.
typedef enum musepack_error {
    MUSEPACK_OK          =  0, ///< success
    MUSEPACK_ERR_INVALID = -1, ///< invalid or unsupported stream
    MUSEPACK_ERR_IO      = -2, ///< reader I/O failure
    MUSEPACK_ERR_NOMEM   = -3, ///< out of memory
    MUSEPACK_ERR_SEEK    = -4, ///< seek failed or source is not seekable
    MUSEPACK_ERR_EOF     = -5, ///< end of stream reached
} musepack_error;

/// Opens a decoder over \p reader and parses the stream header.
///
/// \param reader    reader providing the stream bytes (borrowed, see header)
/// \param error_out optional; receives MUSEPACK_ERR_* on failure (may be NULL)
/// \return an opaque decoder handle, or NULL on failure
musepack_decoder *musepack_decoder_open(mpc_reader *reader,
                                        musepack_error *error_out);

/// Closes a decoder and frees all of its state.
///
/// \param d decoder to close
void musepack_decoder_close(musepack_decoder *d);

/// Copies the stream properties into \p out.
///
/// \param d   decoder
/// \param out receives the streaminfo (must not be NULL)
/// \return MUSEPACK_OK
musepack_error musepack_decoder_get_info(const musepack_decoder *d,
                                         mpc_streaminfo *out);

/// Decodes up to \p max_frames sample-frames into \p pcm.
///
/// On entry \p pcm must have room for at least `max_frames * channels`
/// floats. On return \p frames_out holds the number of sample-frames
/// written (one sample per channel each). `frames_out` may be NULL if the
/// caller only cares about the return code.
///
/// \return MUSEPACK_OK when at least one frame was produced (more data may
///         follow), MUSEPACK_ERR_EOF at end of stream (frames_out == 0),
///         or another MUSEPACK_ERR_* on failure.
musepack_error musepack_decoder_read(musepack_decoder *d, float *pcm,
                                     uint64_t max_frames,
                                     uint64_t *frames_out);

/// Seeks to a 0-based sample position (excluding gapless leading silence).
///
/// \param d      decoder
/// \param sample target sample
/// \return MUSEPACK_OK, MUSEPACK_ERR_SEEK if the source cannot be seeked, or
///         another error. Out-of-range samples clamp to the stream length.
musepack_error musepack_decoder_seek_sample(musepack_decoder *d,
                                            uint64_t sample);

/// Seeks to a time position in seconds.
///
/// \param d       decoder
/// \param seconds target time in seconds
/// \return same semantics as musepack_decoder_seek_sample()
musepack_error musepack_decoder_seek_seconds(musepack_decoder *d,
                                             double seconds);

/// Current playback position in sample-frames since open or the last seek.
///
/// \param d decoder
/// \return number of sample-frames returned so far (0..length_samples)
uint64_t musepack_decoder_position(const musepack_decoder *d);

/// Total playable length in sample-frames (excluding gapless silence).
///
/// \param d decoder
/// \return length in sample-frames
uint64_t musepack_decoder_length_samples(const musepack_decoder *d);

/// Parse-checks the entire stream without producing PCM (mpcdec -c mode).
///
/// Consumes the stream to the end, verifying structure and integrity.
/// \return MUSEPACK_OK if the stream is well-formed, MUSEPACK_ERR_* otherwise.
musepack_error musepack_decoder_check_stream(musepack_decoder *d);

#ifdef __cplusplus
}
#endif
#endif /* MUSEPACK_DECODER_H_ */
