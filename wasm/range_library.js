/*
 * JS imports backing the wasm range reader (mpc_wasm_open_range).
 *
 * These are real wasm imports, so Asyncify reliably suspends the decoder
 * when read() needs to await an HTTP Range fetch. The host (worker/smoke)
 * installs the implementations on the module before calling
 * mpc_wasm_open_range:
 *
 *   Module.mpcRangeRead(ptr, size) -> bytes written into HEAPU8 at ptr
 *   Module.mpcRangeSeek(offset)    -> 1 on success
 *   Module.mpcRangeTell()          -> current offset
 *
 * The read implementation may be async (returns a Promise).
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
