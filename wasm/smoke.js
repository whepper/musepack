/*
 * WASM smoke test for the libmusepack decoder.
 *
 * Loads the Emscripten module, opens a known .mpc fixture, verifies the
 * stream properties and sample count, decodes all PCM to 16-bit WAV, and
 * exits 0 on success. The produced WAV is then compared against the golden
 * fixture by tests/run_wasm_smoke.sh using the tolerance comparator.
 *
 * Usage: node smoke.js <module.js> <input.mpc> <output.wav>
 *   The module is the MODULARIZE=1 output (musepack.js).
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

require(moduleJs)().then((Module) => {
  const h = Module._mpc_wasm_create();
  if (h < 0) fail("mpc_wasm_create failed");

  // Copy the file into WASM linear memory.
  const ptr = Module._malloc(bytes.length);
  Module.HEAPU8.set(bytes, ptr);

  const openErr = Module._mpc_wasm_open(h, ptr, bytes.length);
  if (openErr !== 0) fail(`mpc_wasm_open returned ${openErr}`);

  const rate = Module._mpc_wasm_sample_rate(h);
  const channels = Module._mpc_wasm_channels(h);
  const version = Module._mpc_wasm_stream_version(h);
  const length = Module._mpc_wasm_length_samples(h);

  if (rate !== EXPECTED.rate) fail(`sample rate ${rate} != ${EXPECTED.rate}`);
  if (channels !== EXPECTED.channels) fail(`channels ${channels} != ${EXPECTED.channels}`);
  if (version !== EXPECTED.version) fail(`stream version ${version} != ${EXPECTED.version}`);
  if (length !== EXPECTED.length) fail(`length ${length} != ${EXPECTED.length}`);

  // Decode everything to interleaved float, converting to 16-bit PCM.
  const pcmPtr = Module._malloc(1152 * 2 * 4); // 1152 frames * 2ch * 4B
  const samples16 = Buffer.alloc(length * channels * 2);
  let total = 0;
  for (;;) {
    const frames = Module._mpc_wasm_read(h, pcmPtr, 1152);
    if (frames < 0) {
      if (frames === -5 /* MUSEPACK_ERR_EOF */) break;
      fail(`mpc_wasm_read returned ${frames}`);
    }
    if (frames === 0) fail("mpc_wasm_read stalled with 0 frames");
    const view = new Float32Array(Module.HEAPF32.buffer, pcmPtr, frames * channels);
    for (let i = 0; i < frames * channels; i++) {
      let v = Math.max(-1, Math.min(1, view[i])) * 32767.0;
      samples16.writeInt16LE((v < 0 ? Math.ceil(v) : Math.floor(v)), (total * channels + i) * 2);
    }
    total += frames;
  }

  Module._free(pcmPtr);
  Module._free(ptr);
  Module._mpc_wasm_destroy(h);

  if (total !== EXPECTED.length) fail(`decoded ${total} frames != ${EXPECTED.length}`);
  if (Module._mpc_wasm_position(h) !== 0) fail("position after destroy should read 0");

  // Minimal WAV writer (16-bit PCM).
  const header = Buffer.alloc(44);
  const dataSize = samples16.length;
  header.write("RIFF", 0);
  header.writeUInt32LE(36 + dataSize, 4);
  header.write("WAVE", 8);
  header.write("fmt ", 12);
  header.writeUInt32LE(16, 16);            // fmt chunk size
  header.writeUInt16LE(1, 20);             // PCM
  header.writeUInt16LE(channels, 22);
  header.writeUInt32LE(rate, 24);
  header.writeUInt32LE(rate * channels * 2, 28);
  header.writeUInt16LE(channels * 2, 32);
  header.writeUInt16LE(16, 34);
  header.write("data", 36);
  header.writeUInt32LE(dataSize, 40);
  fs.writeFileSync(outputWav, Buffer.concat([header, samples16]));

  console.log(`wasm smoke ok: rate=${rate} ch=${channels} sv=${version} frames=${total}`);
}).catch((e) => fail(String(e)));
