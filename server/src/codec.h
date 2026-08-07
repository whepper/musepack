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
/// \file codec.h
/// Audio-object stream probing (sample rate / channels / stream version).
///
/// Reads only headers — never decodes the stream. Musepack is probed through
/// libmusepack (header parse); FLAC through the first STREAMINFO block.
/// Codec identification stays separate from MIME (see mime.h).
#ifndef MPSERVER_CODEC_H_
#define MPSERVER_CODEC_H_

#include <musicpack/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mp_codec_info {
    char codec[24];        ///< "musepack-sv8", "musepack-sv7", "flac", ...
    int stream_version;    ///< Musepack only (7/8), else 0
    long long sample_rate; ///< Hz, 0 when unknown
    long long channels;    ///< 0 when unknown
} mp_codec_info;

/// Probes the audio object at \p abs_path (extension from \p rel_path).
/// Never fails on an unreadable/unsupported file: unknown codecs leave the
/// fields zeroed and return MUSICPACK_OK with a best-effort codec name.
musicpack_status mp_codec_probe(const char *abs_path, const char *rel_path,
                                mp_codec_info *out);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_CODEC_H_ */
