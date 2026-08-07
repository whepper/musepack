/*
 * RangeReader + Emscripten callback registration for the libmusepack WASM
 * range decoder (Phase 4 server demo).
 *
 * A RangeReader is a virtual seekable file over a musicpack-server audio
 * URL. read()/seek()/tell() are satisfied by fetching HTTP Range requests on
 * demand through a small chunk LRU. The worker registers three functions
 * into the wasm function table; the decoder (built with ASYNCIFY) calls them
 * synchronously and the async fetches suspend it until data arrives.
 *
 *     server (206)  ->  RangeReader (chunk cache)  ->  libmusepack.wasm
 */
(function (global) {
  'use strict';

  const CHUNK = 65536;
  const MAX_CHUNKS = 16;

  class RangeReader {
    constructor(url, size) {
      this.url = url;
      this.size = size;
      this.pos = 0;
      this.cache = new Map(); // chunkBase -> Uint8Array
    }

    async fetchChunk(base) {
      const end = Math.min(this.size - 1, base + CHUNK - 1);
      const res = await fetch(this.url, {
        headers: { Range: `bytes=${base}-${end}` },
      });
      if (!res.ok || res.status !== 206)
        throw new Error(`Range fetch failed: HTTP ${res.status}`);
      return new Uint8Array(await res.arrayBuffer());
    }

    /* Reads up to `len` bytes at absolute `pos`; returns the bytes read
       (shorter at EOF). Never returns more than requested. */
    async read(pos, len) {
      const out = new Uint8Array(len);
      let have = 0;
      while (have < len) {
        const at = pos + have;
        if (at >= this.size) break;
        const base = Math.floor(at / CHUNK) * CHUNK;
        let chunk = this.cache.get(base);
        if (!chunk) {
          if (this.cache.size >= MAX_CHUNKS)
            this.cache.delete(this.cache.keys().next().value);
          chunk = await this.fetchChunk(base);
          this.cache.set(base, chunk);
        }
        const off = at - base;
        const n = Math.min(CHUNK - off, len - have, this.size - at);
        out.set(chunk.subarray(off, off + n), have);
        have += n;
      }
      return out.subarray(0, have);
    }
  }

  /* Installs read/seek/tell implementations on the module. These are the
     real wasm imports (range_library.js) that Asyncify suspends around.
     Returns the reader so the worker can keep a reference. */
  async function installRangeCallbacks(Module, reader) {
    const HEAPU8 = Module.HEAPU8;

    Module.mpcRangeRead = async function (ptr, size) {
      const data = await reader.read(reader.pos, size);
      if (data.length === 0) return 0;
      HEAPU8.set(data, ptr);
      reader.pos += data.length;
      return data.length;
    };
    Module.mpcRangeSeek = function (offset) {
      reader.pos = offset;
      return 1;
    };
    Module.mpcRangeTell = function () {
      return reader.pos;
    };
    return reader;
  }

  global.MusicPackRange = {
    RangeReader,
    installRangeCallbacks,
  };
})(typeof self !== 'undefined' ? self : this);
