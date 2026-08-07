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
/// \file reader.h
/// libmusepack input abstraction.
///
/// The decoder consumes raw stream bytes exclusively through the
/// \ref mpc_reader interface below. A reader is a set of callbacks plus a
/// caller-owned context pointer (`data`). The decoder never owns the reader
/// and never copies input into its own buffering policy beyond what the
/// demuxer requires internally.
///
/// Sources are pluggable:
///
///     FILE *
///     memory buffer          ->  mpc_reader_init_memory()
///     HTTP Range / custom    ->  implement the callbacks yourself
///
///     FILE *
///     memory buffer
///     HTTP / custom source
///             |
///             v
///         mpc_reader
///             |
///             v
///     libmusepack decoder
///
/// # Built-in adapters
///
///  - mpc_reader_init_stdio()          file reader that owns its FILE *
///  - mpc_reader_init_stdio_stream()   reader over a caller-provided FILE *
///  - mpc_reader_init_memory()         reader over a caller-provided buffer
///
/// # Implementing a custom reader
///
/// Fill an `mpc_reader` and point `data` at your context:
///
///     struct my_ctx { ... };
///     static mpc_int32_t my_read(mpc_reader *r, void *ptr, mpc_int32_t n) {
///         struct my_ctx *c = (struct my_ctx *) r->data;
///         ...
///     }
///     static mpc_bool_t my_seek(mpc_reader *r, mpc_seek_t off) { ... }
///     static mpc_seek_t my_tell(mpc_reader *r) { ... }
///     static mpc_seek_t my_size(mpc_reader *r) { ... }
///     static mpc_bool_t my_canseek(mpc_reader *r) { ... }
///
///     mpc_reader reader = { my_read, my_seek, my_tell, my_size, my_canseek, ctx };
///
/// `read` returns the number of bytes actually read (0 at end of input).
/// `seek` returns MPC_TRUE on success. `get_size` returns 0 if unknown.
/// Non-seekable sources must return MPC_FALSE from `canseek`; seeking
/// operations on such a source fail cleanly with MUSEPACK_ERR_SEEK.
///
/// # Memory adapter ownership
///
/// `mpc_reader_init_memory()` borrows the caller's buffer: the buffer must
/// outlive the reader (and any decoder opened on it). The adapter never
/// frees the data, only its own small bookkeeping structure, which is
/// released by mpc_reader_exit_memory().
#ifndef MUSEPACK_READER_H_
#define MUSEPACK_READER_H_
#pragma once

#include <mpc/reader.h>

#include <musepack/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/// # Reader contract
///
///  - `read` returns the number of bytes actually read and 0 only at the end
///    of available input. Short reads are treated as end-of-available-data by
///    the decoder. Readers that cannot signal a transport error in-band
///    should return 0 on failure; the decoder then reports the stream as
///    truncated (MUSEPACK_ERR_INVALID).
///  - `seek`/`tell`/`get_size` use 64-bit `mpc_seek_t` positions, safe for
///    files and slices larger than 4 GiB.
///  - `get_size` returns 0 when the total size is unknown (e.g. a live
///    stream). Such a reader should also return MPC_FALSE from `canseek`.
///  - `canseek` must return MPC_FALSE for non-seekable sources (network
///    sockets without Range support, pipes). Seeking then fails cleanly with
///    MUSEPACK_ERR_SEEK.
///  - All callbacks receive the reader's `data` pointer, so a custom source
///    (HTTP Range stream, `.mpack` AU slice, browser fetch buffer, iOS/
///    Android networking) carries its state there. The decoder never touches
///    `data` beyond passing it through.
///
/// # Adapter ownership
///
///  - mpc_reader_init_stdio() opens and owns the FILE*; the file is closed by
///    mpc_reader_exit_stdio().
///  - mpc_reader_init_stdio_stream() borrows the caller's FILE*, but the
///    adapter still closes it on exit. Do not fclose() it yourself.
///  - mpc_reader_init_memory() borrows the caller's buffer: the buffer must
///    outlive the reader (and any decoder opened on it). The adapter frees
///    only its own bookkeeping structure on mpc_reader_exit_memory().

/// Initializes a reader over a caller-owned memory buffer.
///
/// \param reader reader handle to initialize
/// \param data   buffer contents (must outlive the reader)
/// \param size   number of valid bytes in \p data
/// \return MPC_STATUS_OK on success, MPC_STATUS_FAIL otherwise
MUSEPACK_API mpc_status mpc_reader_init_memory(mpc_reader *reader,
                                               const void *data,
                                               mpc_seek_t size);

/// Releases a reader created by mpc_reader_init_memory().
/// Does not free the user buffer.
///
/// \param reader reader handle to release
MUSEPACK_API void mpc_reader_exit_memory(mpc_reader *reader);

#ifdef __cplusplus
}
#endif
#endif /* MUSEPACK_READER_H_ */
