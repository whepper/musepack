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
/// \file musepack_wasm.c
/// Emscripten/WebAssembly wrapper for the libmusepack decoder API.
///
/// This is a thin, single-threaded shim over the stable C API. JavaScript
/// holds opaque integer handles (a fixed-slot registry) instead of pointers,
/// copies file bytes into WASM linear memory once, then decodes through the
/// same portable decoder core used natively. No browser-specific logic and
/// no threads/SharedArrayBuffer are required.
///
/// Exported to JS (underscore-prefixed by the toolchain):
///   mpc_wasm_create, mpc_wasm_open, mpc_wasm_sample_rate, mpc_wasm_channels,
///   mpc_wasm_stream_version, mpc_wasm_length_samples, mpc_wasm_position,
///   mpc_wasm_read, mpc_wasm_seek_sample, mpc_wasm_seek_seconds,
///   mpc_wasm_check, mpc_wasm_destroy
///
/// JS usage (after creating the module instance):
///   const h = Module._mpc_wasm_create();
///   Module._mpc_wasm_open(h, dataPtr, dataSize);      // data copied into heap
///   const rate = Module._mpc_wasm_sample_rate(h);
///   for (;;) {
///     const frames = Module._mpc_wasm_read(h, pcmPtr, 1152);
///     if (frames <= 0) break;                          // <0 is EOF/error
///     ... HEAPF32.set / postMessage interleaved float PCM ...
///   }
///   Module._mpc_wasm_destroy(h);

#include <stdlib.h>
#include <string.h>
#include <musepack/musepack.h>

#define WASM_MAX_HANDLES 16

typedef struct {
    int used;
    musepack_decoder *decoder;
    mpc_reader reader;
    mpc_seek_t size;
    intptr_t read_fn;  /* JS function indices (Emscripten addFunction) */
    intptr_t seek_fn;
    intptr_t tell_fn;
    int mem_reader;    /* 1 when reader is the mpc_reader_init_memory adapter */
} wasm_handle;

static wasm_handle handles[WASM_MAX_HANDLES];

static wasm_handle *
wasm_get_handle(int h)
{
    if (h < 0 || h >= WASM_MAX_HANDLES || !handles[h].used)
        return 0;
    return &handles[h];
}

int mpc_wasm_create(void)
{
    int i;
    for (i = 0; i < WASM_MAX_HANDLES; i++) {
        if (!handles[i].used) {
            memset(&handles[i], 0, sizeof handles[i]);
            handles[i].used = 1;
            return i;
        }
    }
    return -1;
}

int mpc_wasm_open(int h, const void *data, unsigned int size)
{
    wasm_handle *hnd = wasm_get_handle(h);
    musepack_error err = MUSEPACK_OK;

    if (hnd == 0 || data == 0)
        return MUSEPACK_ERR_INVALID;
    if (hnd->decoder != 0) {
        musepack_decoder_close(hnd->decoder);
        if (hnd->mem_reader)
            mpc_reader_exit_memory(&hnd->reader);
        hnd->decoder = 0;
    }
    if (mpc_reader_init_memory(&hnd->reader, data, size) != MPC_STATUS_OK)
        return MUSEPACK_ERR_INVALID;
    hnd->mem_reader = 1;
    hnd->decoder = musepack_decoder_open(&hnd->reader, &err);
    return hnd->decoder != 0 ? MUSEPACK_OK : (int) err;
}

/* ---- range reader: the decoder's reads go through JS callbacks ---------
   The JS side owns a virtual file and fulfills read/seek/tell by issuing
   HTTP Range requests against a musicpack-server. The callbacks may be
   async; the module is built with ASYNCIFY, so decoder calls that touch the
   reader return a Promise to JS (await them). Function indices come from
   Emscripten Module.addFunction(). */

static mpc_int32_t
js_read(mpc_reader *reader, void *ptr, mpc_int32_t size)
{
    wasm_handle *h = (wasm_handle *) reader->data;
    /* read_fn(ptr:int, size:int) -> int bytes written into wasm memory */
    return ((mpc_int32_t (*)(mpc_int32_t, mpc_int32_t))
            (intptr_t) h->read_fn)((mpc_int32_t) (intptr_t) ptr, size);
}

static mpc_bool_t
js_seek(mpc_reader *reader, mpc_seek_t offset)
{
    wasm_handle *h = (wasm_handle *) reader->data;
    /* seek_fn(offset:double) -> int (1 ok) */
    return (mpc_bool_t) ((int (*)(double)) (intptr_t) h->seek_fn)((double) offset);
}

static mpc_seek_t
js_tell(mpc_reader *reader)
{
    wasm_handle *h = (wasm_handle *) reader->data;
    /* tell_fn() -> double */
    return (mpc_seek_t) ((double (*)(void)) (intptr_t) h->tell_fn)();
}

static mpc_seek_t
js_size(mpc_reader *reader)
{
    wasm_handle *h = (wasm_handle *) reader->data;
    return h->size;
}

static mpc_bool_t
js_canseek(mpc_reader *reader)
{
    (void) reader;
    return MPC_TRUE;
}

int mpc_wasm_open_range(int h, double size, int read_fn, int seek_fn,
                        int tell_fn)
{
    wasm_handle *hnd = wasm_get_handle(h);
    musepack_error err = MUSEPACK_OK;

    if (hnd == 0 || size <= 0 || read_fn <= 0 || seek_fn <= 0 || tell_fn <= 0)
        return MUSEPACK_ERR_INVALID;
    if (hnd->decoder != 0) {
        musepack_decoder_close(hnd->decoder);
        if (hnd->mem_reader)
            mpc_reader_exit_memory(&hnd->reader);
        hnd->decoder = 0;
    }
    hnd->mem_reader = 0;
    hnd->size = (mpc_seek_t) size;
    hnd->read_fn = read_fn;
    hnd->seek_fn = seek_fn;
    hnd->tell_fn = tell_fn;
    hnd->reader.read = js_read;
    hnd->reader.seek = js_seek;
    hnd->reader.tell = js_tell;
    hnd->reader.get_size = js_size;
    hnd->reader.canseek = js_canseek;
    hnd->reader.data = hnd;
    hnd->decoder = musepack_decoder_open(&hnd->reader, &err);
    return hnd->decoder != 0 ? MUSEPACK_OK : (int) err;
}

int mpc_wasm_sample_rate(int h)
{
    wasm_handle *hnd = wasm_get_handle(h);
    mpc_streaminfo si;
    if (hnd == 0 || hnd->decoder == 0)
        return 0;
    if (musepack_decoder_get_info(hnd->decoder, &si) != MUSEPACK_OK)
        return 0;
    return (int) si.sample_freq;
}

int mpc_wasm_channels(int h)
{
    wasm_handle *hnd = wasm_get_handle(h);
    mpc_streaminfo si;
    if (hnd == 0 || hnd->decoder == 0)
        return 0;
    if (musepack_decoder_get_info(hnd->decoder, &si) != MUSEPACK_OK)
        return 0;
    return (int) si.channels;
}

int mpc_wasm_stream_version(int h)
{
    wasm_handle *hnd = wasm_get_handle(h);
    mpc_streaminfo si;
    if (hnd == 0 || hnd->decoder == 0)
        return 0;
    if (musepack_decoder_get_info(hnd->decoder, &si) != MUSEPACK_OK)
        return 0;
    return (int) si.stream_version;
}

double mpc_wasm_length_samples(int h)
{
    wasm_handle *hnd = wasm_get_handle(h);
    if (hnd == 0 || hnd->decoder == 0)
        return 0;
    return (double) musepack_decoder_length_samples(hnd->decoder);
}

double mpc_wasm_position(int h)
{
    wasm_handle *hnd = wasm_get_handle(h);
    if (hnd == 0 || hnd->decoder == 0)
        return 0;
    return (double) musepack_decoder_position(hnd->decoder);
}

/// Decodes up to max_frames sample-frames into ptr (interleaved float).
/// Returns the number of frames written (positive), or a negative
/// musepack_error (MUSEPACK_ERR_EOF at end of stream).
int mpc_wasm_read(int h, float *ptr, int max_frames)
{
    wasm_handle *hnd = wasm_get_handle(h);
    uint64_t frames = 0;
    musepack_error e;

    if (hnd == 0 || hnd->decoder == 0 || ptr == 0 || max_frames <= 0)
        return MUSEPACK_ERR_INVALID;
    e = musepack_decoder_read(hnd->decoder, ptr, (uint64_t) max_frames, &frames);
    if (e == MUSEPACK_OK)
        return (int) frames;
    if (e == MUSEPACK_ERR_EOF)
        return MUSEPACK_ERR_EOF;
    return (int) e;
}

int mpc_wasm_seek_sample(int h, double sample)
{
    wasm_handle *hnd = wasm_get_handle(h);
    if (hnd == 0 || hnd->decoder == 0 || sample < 0)
        return MUSEPACK_ERR_INVALID;
    return (int) musepack_decoder_seek_sample(hnd->decoder, (uint64_t) sample);
}

int mpc_wasm_seek_seconds(int h, double seconds)
{
    wasm_handle *hnd = wasm_get_handle(h);
    if (hnd == 0 || hnd->decoder == 0 || seconds < 0)
        return MUSEPACK_ERR_INVALID;
    return (int) musepack_decoder_seek_seconds(hnd->decoder, seconds);
}

int mpc_wasm_check(int h)
{
    wasm_handle *hnd = wasm_get_handle(h);
    if (hnd == 0 || hnd->decoder == 0)
        return MUSEPACK_ERR_INVALID;
    return (int) musepack_decoder_check_stream(hnd->decoder);
}

void mpc_wasm_destroy(int h)
{
    wasm_handle *hnd = wasm_get_handle(h);
    if (hnd == 0)
        return;
    if (hnd->decoder != 0) {
        musepack_decoder_close(hnd->decoder);
        if (hnd->mem_reader)
            mpc_reader_exit_memory(&hnd->reader);
        hnd->decoder = 0;
    }
    memset(hnd, 0, sizeof *hnd);
}
