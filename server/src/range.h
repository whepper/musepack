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
/// \file range.h
/// RFC 7233 single-range byte serving parser (strict).
///
/// Pure, bounded, fuzzable. Only a single `bytes=...` range is supported
/// (multipart/multiple ranges are explicitly out of Phase 4 scope).
#ifndef MPSERVER_RANGE_H_
#define MPSERVER_RANGE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mp_range {
    long long start;   ///< inclusive first byte
    long long length;  ///< number of bytes (>= 0)
} mp_range;

typedef enum {
    MP_RANGE_NONE,          ///< no usable Range header (serve 200 full file)
    MP_RANGE_OK,            ///< out holds a satisfiable range
    MP_RANGE_INVALID,       ///< malformed, non-bytes unit, or multiple ranges
    MP_RANGE_UNSATISFIABLE, ///< first-byte-pos >= size (serve 416)
} mp_range_result;

/// Parses a single-range `Range` header value against \p size.
///
/// Handles `bytes=0-1023`, `bytes=1024-`, `bytes=-4096`. Clamps an end beyond
/// EOF to the file end. Empty ranges and `bytes=0-0` on a zero-length file
/// follow RFC 7233 (unsatisfiable when start >= size, else satisfiable).
mp_range_result mp_range_parse(const char *header, long long size,
                               mp_range *out);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_RANGE_H_ */
