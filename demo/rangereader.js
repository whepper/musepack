/*
 * RangeReader + Emscripten callback registration for the libmusepack WASM
 * range decoder (Phase 4 server demo).
 *
 * The track is fetched from the musicpack-server over HTTP Range (chunked,
 * each chunk a 206 response), then decoded through mpc_wasm_open_range with
 * synchronous JS callbacks serving the assembled buffer. This proves the
 * transport uses Range end-to-end while keeping the decoder single-threaded
 * and Asyncify-free (a future SAB/Atomics reader can go fully on-demand
 * behind the same imports).
 */
(function (global) {
  'use strict';

  const CHUNK = 65536;

  class RangeReader {
    constructor(url, size) {
      this.url = url;
      this.size = size;
      this.pos = 0;
      this.buffer = null; // Uint8Array, populated by fetchAll()
    }

    /* Fetches the whole object over HTTP Range, verifying each 206 and the
       assembled length/size. Returns the bytes. */
    async fetchAll() {
      const parts = [];
      let offset = 0;
      while (offset < this.size) {
        const end = Math.min(this.size - 1, offset + CHUNK - 1);
        const res = await fetch(this.url, {
          headers: { Range: `bytes=${offset}-${end}` },
        });
        if (!res.ok || res.status !== 206)
          throw new Error(`Range fetch failed: HTTP ${res.status}`);
        const chunk = new Uint8Array(await res.arrayBuffer());
        parts.push(chunk);
        offset += chunk.length;
      }
      const total = parts.reduce((n, p) => n + p.length, 0);
      if (total !== this.size)
        throw new Error(`assembled ${total} bytes, expected ${this.size}`);
      const buf = new Uint8Array(total);
      let at = 0;
      for (const p of parts) { buf.set(p, at); at += p.length; }
      this.buffer = buf;
      return buf;
    }
  }

  /* Installs SYNCHRONOUS read/seek/tell implementations on the module (the
     wasm imports in range_library.js). Requires reader.fetchAll() to have
     run. */
  async function installRangeCallbacks(Module, reader) {
    const HEAPU8 = Module.HEAPU8;
    const buf = reader.buffer;
    if (!buf) throw new Error('RangeReader buffer not fetched');

    Module.mpcRangeRead = function (ptr, size) {
      const n = Math.min(size, buf.length - reader.pos);
      if (n <= 0) return 0;
      HEAPU8.set(buf.subarray(reader.pos, reader.pos + n), ptr);
      reader.pos += n;
      return n;
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
