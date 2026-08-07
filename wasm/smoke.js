/*
 * WASM smoke test for the libmusepack decoder.
 *
 * Loads the Emscripten module, opens a known .mpc fixture two ways:
 *   1. the memory reader (mpc_wasm_open) — the historical path;
 *   2. the JS-callback range reader (mpc_wasm_open_range) backed by a fake
 *      byte-range source — this is the musicpack-server HTTP Range reader
 *      plumbing, exercised end-to-end in Node.
 * Verifies both produce identical PCM and that seeking works over the range
 * reader, then writes the memory-path PCM to a 16-bit WAV. The WAV is
 * compared against the golden fixture by tests/run_wasm_smoke.sh.
 *
 * Usage: node smoke.js <module.js> <input.mpc> <output.wav>
 */

const fs = require("fs");
const path = require("path");

const moduleJs = process.argv[2];
const inputMpc = process.argv[3];
const outputWav = process.argv[4];

if (!moduleJs || !inputMpc || !outputWav) {
  console.error("usage: node smoke.js <module.js> <input.mpc> <output.wav>");
  process.exit(2);
}

function fail(msg) {
  console.error("FAIL:", msg);
  process.exit(1);
}

const bytes = fs.readFileSync(inputMpc);

// Known properties of tests/fixtures/sine44-q5.mpc.
const EXPECTED = { rate: 44100, channels: 2, version: 8, length: 44100 };

/* Decodes everything on handle `h` into an interleaved Float32Array. */
async function decodeAll(Module, h, channels) {
  const pcmPtr = Module._malloc(1152 * channels * 4);
  const out = [];
  for (;;) {
    const frames = await Module._mpc_wasm_read(h, pcmPtr, 1152);
    if (frames < 0) {
      if (frames === -5 /* MUSEPACK_ERR_EOF */) break;
      fail(`mpc_wasm_read returned ${frames}`);
    }
    if (frames === 0) fail("mpc_wasm_read stalled with 0 frames");
    const view = new Float32Array(Module.HEAPF32.buffer, pcmPtr, frames * channels);
    out.push(...view);
  }
  Module._free(pcmPtr);
  return Float32Array.from(out);
}

function toS16(pcm, channels) {
  const samples16 = Buffer.alloc(pcm.length * 2);
  for (let i = 0; i < pcm.length; i++) {
    let v = Math.max(-1, Math.min(1, pcm[i])) * 32767.0;
    samples16.writeInt16LE((v < 0 ? Math.ceil(v) : Math.floor(v)), i * 2);
  }
  return samples16;
}

require(moduleJs)().then(async (Module) => {
  // ---- path 1: memory reader (historical path)
  const h1 = Module._mpc_wasm_create();
  if (h1 < 0) fail("mpc_wasm_create failed");

  const memPtr = Module._malloc(bytes.length);
  Module.HEAPU8.set(bytes, memPtr);
  const openErr = await Module._mpc_wasm_open(h1, memPtr, bytes.length);
  if (openErr !== 0) fail(`mpc_wasm_open returned ${openErr}`);

  const rate = Module._mpc_wasm_sample_rate(h1);
  const channels = Module._mpc_wasm_channels(h1);
  const version = Module._mpc_wasm_stream_version(h1);
  const length = Module._mpc_wasm_length_samples(h1);

  if (rate !== EXPECTED.rate) fail(`sample rate ${rate} != ${EXPECTED.rate}`);
  if (channels !== EXPECTED.channels) fail(`channels ${channels} != ${EXPECTED.channels}`);
  if (version !== EXPECTED.version) fail(`stream version ${version} != ${EXPECTED.version}`);
  if (length !== EXPECTED.length) fail(`length ${length} != ${EXPECTED.length}`);

  const pcmMem = await decodeAll(Module, h1, channels);
  Module._free(memPtr);
  Module._mpc_wasm_destroy(h1);
  if (pcmMem.length / channels !== EXPECTED.length)
    fail(`decoded ${pcmMem.length / channels} frames != ${EXPECTED.length}`);

  // ---- path 2: JS range reader (musicpack-server HTTP Range plumbing).
  //      Install the implementations on the module; the fake source serves
  //      byte ranges from `bytes`, like the browser RangeReader does over
  //      HTTP. The read implementation is async, exercising Asyncify.
  const h2 = Module._mpc_wasm_create();
  if (h2 < 0) fail("mpc_wasm_create (range) failed");
  let rpos = 0;
  Module.mpcRangeRead = function (ptr, size) {
    const n = Math.min(size, bytes.length - rpos);
    if (n <= 0) return 0;
    Module.HEAPU8.set(bytes.subarray(rpos, rpos + n), ptr);
    rpos += n;
    return n;
  };
  Module.mpcRangeSeek = function (offset) { rpos = offset; return 1; };
  Module.mpcRangeTell = function () { return rpos; };

  const rangeErr = await Module._mpc_wasm_open_range(h2, bytes.length);
  if (rangeErr !== 0) fail(`mpc_wasm_open_range returned ${rangeErr}`);

  const s1 = await Module._mpc_wasm_seek_sample(h2, 22050);
  if (s1 !== 0) fail(`range seek mid failed ${s1}`);
  const s2 = await Module._mpc_wasm_seek_sample(h2, 0);
  if (s2 !== 0) fail(`range seek back failed ${s2}`);

  const pcmRange = await decodeAll(Module, h2, channels);
  Module._mpc_wasm_destroy(h2);
  Module.mpcRangeRead = Module.mpcRangeSeek = Module.mpcRangeTell = null;

  if (pcmRange.length !== pcmMem.length)
    fail(`range decoded ${pcmRange.length} samples != memory ${pcmMem.length}`);
  for (let i = 0; i < pcmMem.length; i++) {
    if (pcmRange[i] !== pcmMem[i])
      fail(`range/memory PCM differ at sample ${i}`);
  }

  // ---- WAV output from the memory path
  const samples16 = toS16(pcmMem, channels);
  const header = Buffer.alloc(44);
  const dataSize = samples16.length;
  header.write("RIFF", 0);
  header.writeUInt32LE(36 + dataSize, 4);
  header.write("WAVE", 8);
  header.write("fmt ", 12);
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(channels, 22);
  header.writeUInt32LE(rate, 24);
  header.writeUInt32LE(rate * channels * 2, 28);
  header.writeUInt16LE(channels * 2, 32);
  header.writeUInt16LE(16, 34);
  header.write("data", 36);
  header.writeUInt32LE(dataSize, 40);
  fs.writeFileSync(outputWav, Buffer.concat([header, samples16]));

  console.log(`wasm smoke ok: rate=${rate} ch=${channels} sv=${version} ` +
              `frames=${pcmMem.length / channels} range-reader=${pcmRange.length / channels}`);
}).catch((e) => fail(String(e)));
