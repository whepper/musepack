/*
 * JS imports backing the wasm range reader (mpc_wasm_open_range).
 *
 * The host (worker/smoke) installs SYNCHRONOUS implementations on the module
 * before calling mpc_wasm_open_range:
 *
 *   Module.mpcRangeRead(ptr, size) -> bytes written into HEAPU8 at ptr
 *   Module.mpcRangeSeek(offset)    -> 1 on success
 *   Module.mpcRangeTell()          -> current offset
 *
 * The Phase 4 demo fetches the track from the server over HTTP Range first,
 * so the reader serves from an in-memory buffer and stays synchronous. This
 * keeps the decoder single-threaded and sidesteps Asyncify (which cannot
 * suspend the decoder's many sequential reads during a seek). A future
 * SAB/Atomics-backed reader can use the same imports to go fully
 * demand-driven.
 */
var LibraryMusicPackRange = {
  mpc_range_read: function (ptr, size) {
    return Module.mpcRangeRead(ptr, size);
  },
  mpc_range_seek: function (offset) {
    return Module.mpcRangeSeek(offset);
  },
  mpc_range_tell: function () {
    return Module.mpcRangeTell();
  },
};

mergeInto(LibraryManager.library, LibraryMusicPackRange);
