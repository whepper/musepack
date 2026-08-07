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
/// \file error.h
/// libmusicpack status codes.
#ifndef MUSICPACK_ERROR_H_
#define MUSICPACK_ERROR_H_

#include <musicpack/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Status codes. Success is 0; all errors are negative.
typedef enum musicpack_status {
    MUSICPACK_OK            =  0, ///< success
    MUSICPACK_ERR_INVALID   = -1, ///< invalid argument or malformed content
    MUSICPACK_ERR_JSON      = -2, ///< manifest is not well-formed JSON
    MUSICPACK_ERR_VERSION   = -3, ///< unsupported manifest format/version
    MUSICPACK_ERR_IO        = -4, ///< filesystem or I/O failure
    MUSICPACK_ERR_NOMEM     = -5, ///< out of memory
    MUSICPACK_ERR_CHECKSUM  = -6, ///< sha256 mismatch
    MUSICPACK_ERR_PATH      = -7, ///< unsafe, absolute or traversing path
    MUSICPACK_ERR_MISSING   = -8, ///< a referenced file is absent
} musicpack_status;

#ifdef __cplusplus
}
#endif
#endif /* MUSICPACK_ERROR_H_ */
