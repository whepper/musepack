/*
 * WASM smoke test for the libmusepack decoder.
 *
 * Loads the Emscripten module and opens a known .mpc fixture three ways:
 *   1. the memory reader (mpc_wasm_open) — the historical path;
 *   2. the JS range reader (mpc_wasm_open_range) backed by a fake
 *      byte-range source — the musicpack-server HTTP Range plumbing;
 *   3. network-failure injection through the same reader (401/503/network/
 *      200/truncated/bad-range) to verify failures surface cleanly.
 * Verifies the range reader produces PCM identical to the memory path, that
 * seeking to 90% does NOT fetch the whole file, and writes the memory-path
 * PCM to a 16-bit WAV. The WAV is compared against the golden fixture by
 * tests/run_wasm_smoke.sh.
 *
 * Usage: node smoke.js <module.js> <input.mpc> <output.wav>
 */

const fs = require("fs");
const path = require("path");
const { Worker } = require("worker_threads");

const M = require("../demo/reader_mailbox.js");

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

/* Known properties of tests/fixtures/sine44-q5-48s.mpc (48 s @ 44.1 kHz). */
const EXPECTED = { rate: 44100, channels: 2, version: 8, length: 2116800 };

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

/* Decodes at most `maxFrames` frames (for the partial-decode accounting). */
async function decodeFrames(Module, h, channels, maxFrames) {
  const pcmPtr = Module._malloc(1152 * channels * 4);
  let got = 0;
  const out = [];
  while (got < maxFrames) {
    const frames = await Module._mpc_wasm_read(h, pcmPtr, 1152);
    if (frames < 0 || frames === 0) break;
    const view = new Float32Array(Module.HEAPF32.buffer, pcmPtr, frames * channels);
    for (let i = 0; i < frames * channels; i++) out.push(view[i]);
    got += frames;
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

/* Spawns a smoke_networker worker and installs the Atomics-based reader. */
function createDemandReader(Module, data, failMode) {
  const sab = new SharedArrayBuffer(M.DATA_OFFSET + M.DATA_CAP);
  const state = new Int32Array(sab);
  const dataView = new Uint8Array(sab, M.DATA_OFFSET, M.DATA_CAP);
  const srcSab = new SharedArrayBuffer(data.length);
  new Uint8Array(srcSab).set(data);

  const worker = new Worker(path.join(__dirname, "smoke_networker.js"));
  const ready = new Promise((resolve, reject) => {
    worker.on("message", (m) => { if (m.type === "ready") resolve(); });
    worker.on("error", reject);
  });
  worker.postMessage({
    type: "open", sab, sourceSab: srcSab, size: data.length, failMode,
  });

  let readerPos = 0;
  let lastError = 0;
  const HEAPU8 = Module.HEAPU8;

  function rangeRead(ptr, size) {
    const pos = readerPos;
    const want = Math.min(size, M.DATA_CAP, data.length - pos);
    if (want <= 0) return 0;
    Atomics.store(state, M.POS_LO, pos >>> 0);
    Atomics.store(state, M.POS_HI, Math.floor(pos / 4294967296));
    Atomics.store(state, M.LEN, want);
    Atomics.store(state, M.RES, 0);
    Atomics.store(state, M.REQ, 1);
    Atomics.notify(state, M.REQ);
    while (Atomics.load(state, M.RES) === 0)
      Atomics.wait(state, M.RES, 0);
    if (Atomics.load(state, M.ERROR) !== 0) {
      lastError = Atomics.load(state, M.ERROR);
      Atomics.store(state, M.RES, 0);
      return 0;
    }
    const n = Atomics.load(state, M.DONE_LEN);
    HEAPU8.set(dataView.subarray(0, n), ptr);
    readerPos += n;
    Atomics.store(state, M.RES, 0);
    return n;
  }
  function rangeSeek(offset) { readerPos = offset; return 1; }
  function rangeTell() { return readerPos; }

  return {
    ready,
    install() {
      Module.mpcRangeRead = rangeRead;
      Module.mpcRangeSeek = rangeSeek;
      Module.mpcRangeTell = rangeTell;
    },
    served() { return Atomics.load(state, M.SERVED); },
    lastError() { return lastError; },
    close() {
      worker.terminate();
      Module.mpcRangeRead = Module.mpcRangeSeek = Module.mpcRangeTell = null;
    },
  };
}

/* Demand-driven range path: seeking to 90% must not fetch the whole file. */
async function demandPath(Module, data, channels) {
  const h = Module._mpc_wasm_create();
  if (h < 0) fail("mpc_wasm_create (range) failed");
  const d = createDemandReader(Module, data, null);
  await d.ready;
  d.install();

  const openErr = await Module._mpc_wasm_open_range(h, data.length);
  if (openErr !== 0) fail(`mpc_wasm_open_range returned ${openErr}`);
  const total = data.length;
  const samples = Module._mpc_wasm_length_samples(h);

  const afterOpen = d.served();

  await Module._mpc_wasm_seek_sample(h, Math.floor(samples * 0.9));
  await decodeFrames(Module, h, channels, 5);
  const after90 = d.served();
  if (after90 - afterOpen > 2 * M.BLOCK)
    fail(`seek to 90%% fetched ${after90 - afterOpen} new bytes beyond open (limit ${2 * M.BLOCK})`);
  if (after90 >= total)
    fail(`seek to 90%% downloaded the whole file (${after90}/${total})`);

  await Module._mpc_wasm_seek_sample(h, 0);
  await decodeFrames(Module, h, channels, 5);
  await Module._mpc_wasm_seek_sample(h, Math.floor(samples * 0.25));
  await decodeFrames(Module, h, channels, 5);
  await Module._mpc_wasm_seek_sample(h, Math.floor(samples * 0.5));
  await decodeFrames(Module, h, channels, 5);
  await Module._mpc_wasm_seek_sample(h, Math.floor(samples * 0.9));
  await decodeFrames(Module, h, channels, 5);

  // full decode from 0 for byte-correct PCM
  await Module._mpc_wasm_seek_sample(h, 0);
  const pcm = await decodeAll(Module, h, channels);
  const afterFull = d.served();
  Module._mpc_wasm_destroy(h);
  d.close();
  if (afterFull < total)
    fail(`full decode served only ${afterFull}/${total} bytes`);
  return { pcm, afterOpen, after90, afterFull, total };
}

/* Failure modes must surface as clean reader errors, not bad PCM. */
async function failureModes(Module, data) {
  const cases = [
    ["http401", M.ERR_HTTP],
    ["http503", M.ERR_HTTP],
    ["network", M.ERR_NETWORK],
    ["200", M.ERR_200],
    ["truncated", M.ERR_TRUNCATED],
    ["badrange", M.ERR_RANGE],
  ];
  for (const [mode, expectErr] of cases) {
    const h = Module._mpc_wasm_create();
    const d = createDemandReader(Module, data, mode);
    await d.ready;
    d.install();
    const openErr = await Module._mpc_wasm_open_range(h, data.length);
    if (openErr === 0)
      fail(`failMode '${mode}': open_range unexpectedly succeeded`);
    if (d.lastError() !== expectErr)
      fail(`failMode '${mode}': expected reader error ${expectErr}, got ${d.lastError()}`);
    Module._mpc_wasm_destroy(h);
    d.close();
  }
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

  // ---- path 2: demand-driven range reader (Phase 5)
  const dr = await demandPath(Module, bytes, channels);
  if (dr.pcm.length !== pcmMem.length)
    fail(`range decoded ${dr.pcm.length} samples != memory ${pcmMem.length}`);
  for (let i = 0; i < pcmMem.length; i++) {
    if (dr.pcm[i] !== pcmMem[i])
      fail(`range/memory PCM differ at sample ${i}`);
  }
  console.log(`  range: open=${dr.afterOpen}B seek90=${dr.after90}B full=${dr.afterFull}B of ${dr.total}B`);

  // ---- path 3: network failure modes
  await failureModes(Module, bytes);

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
              `frames=${pcmMem.length / channels} range-reader=ok`);
}).catch((e) => fail(String(e)));
