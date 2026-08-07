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
///     musepack_stream_info info;
///     info.size = sizeof(info);
///     musepack_decoder_get_stream_info(d, &info);
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
///  - musepack_decoder_close() may be passed NULL (it is a no-op). Calling
///    close() twice on the same decoder, or using a decoder after close(),
///    is undefined behaviour.
///  - musepack_decoder_open() parses the header immediately and fails
///    (returning NULL) on unreadable or invalid input. Opening consumes
///    header bytes from the reader, so the reader's position after open is
///    unspecified. A reader may be reused for a later decoder only after it
///    has been repositioned to the stream start.
///  - create/open/decode/seek/close may be repeated any number of times on
///    fresh readers; each decoder is fully independent.
///
/// # Error handling
///
/// Every call that can fail reports a \ref musepack_error. Errors are
/// negative; MUSEPACK_OK (0) means success. A failed read/seek does not
/// invalidate the decoder; the call may be retried. Error codes actually
/// produced by the current decoder:
///
///  - MUSEPACK_ERR_INVALID — open (unreadable/unsupported stream), read and
///    check_stream (malformed or truncated data), bad arguments.
///  - MUSEPACK_ERR_NOMEM — open (allocation failure).
///  - MUSEPACK_ERR_SEEK — seek on a non-seekable reader or a failed seek.
///  - MUSEPACK_ERR_EOF — end of stream (read only).
///  - MUSEPACK_ERR_IO is reserved for future readers that can distinguish
///    transport failures; the current decoder reports such conditions as
///    MUSEPACK_ERR_INVALID.
///
/// # Thread-safety contract
///
/// No explicit library initialization is required; tables are built once on
/// first use. Distinct decoder instances (each with its own reader) may be
/// used concurrently from different threads. A single decoder instance and
/// its reader are not thread-safe and must not be shared without external
/// locking; a reader must not be shared between decoders on different
/// threads.
#ifndef MUSEPACK_DECODER_H_
#define MUSEPACK_DECODER_H_
#pragma once

#include <stdint.h>
#include <mpc/reader.h>

#include <musepack/export.h>
#include <musepack/reader.h>
#include <musepack/streaminfo.h>
#include <musepack/version.h>

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
    MUSEPACK_ERR_INVALID = -1, ///< invalid or unsupported stream or argument
    MUSEPACK_ERR_IO      = -2, ///< reader I/O failure (reserved, see above)
    MUSEPACK_ERR_NOMEM   = -3, ///< out of memory
    MUSEPACK_ERR_SEEK    = -4, ///< seek failed or source is not seekable
    MUSEPACK_ERR_EOF     = -5, ///< end of stream reached
} musepack_error;

/// Returns the runtime version string of the library (e.g. "7.0.1").
/// The returned pointer is static and owned by the library.
MUSEPACK_API const char *musepack_version(void);

/// Opens a decoder over \p reader and parses the stream header.
///
/// \param reader    reader providing the stream bytes (borrowed, see header)
/// \param error_out optional; receives MUSEPACK_ERR_* on failure (may be NULL)
/// \return an opaque decoder handle, or NULL on failure
MUSEPACK_API musepack_decoder *musepack_decoder_open(mpc_reader *reader,
                                                     musepack_error *error_out);

/// Closes a decoder and frees all of its state. NULL is a no-op.
///
/// \param d decoder to close
MUSEPACK_API void musepack_decoder_close(musepack_decoder *d);

/// Copies the stream properties into \p out (versioned structure).
///
/// On entry \p out->size must equal `sizeof(musepack_stream_info)`. The
/// library writes at most \p out->size bytes, so consumers compiled against
/// an older, smaller layout receive the leading fields.
///
/// \param d   decoder
/// \param out receives the streaminfo (must not be NULL, size field set)
/// \return MUSEPACK_OK, or MUSEPACK_ERR_INVALID on bad arguments
MUSEPACK_API musepack_error musepack_decoder_get_stream_info(
    const musepack_decoder *d, musepack_stream_info *out);

/// Legacy: copies the historical mpc_streaminfo structure.
///
/// Provided for compatibility with the pre-Phase-1 header set. Prefer
/// musepack_decoder_get_stream_info() for new code.
///
/// \param d   decoder
/// \param out receives the streaminfo (must not be NULL)
/// \return MUSEPACK_OK, or MUSEPACK_ERR_INVALID on bad arguments
MUSEPACK_API musepack_error musepack_decoder_get_info(const musepack_decoder *d,
                                                      mpc_streaminfo *out);

/// Stream version (7 or 8).
MUSEPACK_API uint32_t musepack_decoder_stream_version(const musepack_decoder *d);
/// Sample rate in Hz (0 if \p d is NULL).
MUSEPACK_API uint32_t musepack_decoder_sample_rate(const musepack_decoder *d);
/// Channel count (0 if \p d is NULL).
MUSEPACK_API uint32_t musepack_decoder_channels(const musepack_decoder *d);

/// Decodes up to \p max_frames sample-frames into \p pcm.
///
/// A sample-frame is one sample per channel; PCM is interleaved in the order
/// L, R, L, R, ... (stereo) as single-precision float in ~[-1, 1]. \p pcm
/// must have room for at least `max_frames * channels` floats.
///
/// On success \p frames_out holds the number of sample-frames written and is
/// equal to \p max_frames, unless the stream ended within this call, in
/// which case it holds the remaining frames and the next call returns
/// MUSEPACK_ERR_EOF. `frames_out` may be NULL.
///
/// End of stream is a normal state, not an error: it is reported by
/// MUSEPACK_ERR_EOF with `frames_out == 0`, and every subsequent read keeps
/// returning MUSEPACK_ERR_EOF. To replay, seek to 0 first.
///
/// A failed read may have produced valid frames in \p pcm (reported in
/// \p frames_out); the caller should consume them before retrying. Errors do
/// not invalidate the decoder.
///
/// \param d          decoder
/// \param pcm        destination buffer (interleaved float)
/// \param max_frames maximum sample-frames to decode (must be > 0)
/// \param frames_out receives the number of sample-frames written (may be NULL)
/// \return MUSEPACK_OK when at least one frame was produced, MUSEPACK_ERR_EOF
///         at end of stream, or another MUSEPACK_ERR_* on failure
MUSEPACK_API musepack_error musepack_decoder_read(musepack_decoder *d,
                                                  float *pcm,
                                                  uint64_t max_frames,
                                                  uint64_t *frames_out);

/// Seeks to a 0-based sample-frame position, excluding gapless leading
/// silence. Positions are per-channel sample-frames, not per-channel-sample
/// halves, and are relative to the start of playable audio.
///
/// Values beyond the stream length clamp to the end; seeking to the end
/// makes subsequent reads return MUSEPACK_ERR_EOF immediately. After a
/// successful seek the decode position is exactly the (clamped) target and
/// musepack_decoder_position() reports it. Negative positions are not
/// representable (the argument is unsigned); musepack_decoder_seek_seconds()
/// rejects negative times.
///
/// \param d      decoder
/// \param sample target sample-frame
/// \return MUSEPACK_OK, MUSEPACK_ERR_SEEK if the reader cannot seek, or
///         another MUSEPACK_ERR_* on failure
MUSEPACK_API musepack_error musepack_decoder_seek_sample(musepack_decoder *d,
                                                         uint64_t sample);

/// Seeks to a time position in seconds (0-based), rounded to the nearest
/// sample-frame (round-half-up). Negative values are rejected.
///
/// \param d       decoder
/// \param seconds target time in seconds
/// \return same semantics as musepack_decoder_seek_sample()
MUSEPACK_API musepack_error musepack_decoder_seek_seconds(musepack_decoder *d,
                                                          double seconds);

/// Current playback position in sample-frames since open or the last seek.
///
/// \param d decoder
/// \return number of sample-frames returned so far (0..length_samples)
MUSEPACK_API uint64_t musepack_decoder_position(const musepack_decoder *d);

/// Total playable length in sample-frames (excluding gapless silence).
///
/// \param d decoder
/// \return length in sample-frames
MUSEPACK_API uint64_t musepack_decoder_length_samples(const musepack_decoder *d);

/// Parse-checks the entire stream without producing PCM (mpcdec -c mode).
///
/// Consumes the stream to the end, verifying structure and integrity.
/// \return MUSEPACK_OK if the stream is well-formed, MUSEPACK_ERR_* otherwise.
MUSEPACK_API musepack_error musepack_decoder_check_stream(musepack_decoder *d);

#ifdef __cplusplus
}
#endif
#endif /* MUSEPACK_DECODER_H_ */
