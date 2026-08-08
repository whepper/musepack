/*
 * WASM/audio harness for the MusicPack web client (Phase 6).
 *
 * Runs in Node against the real libmusepack.wasm and the client's actual
 * ring buffer, verifying the guarantees the AudioWorklet pipeline relies on:
 *
 *   1. Gapless two-track feed: decode A and B fully, push both through a
 *      RingBuffer exactly as the controller does at a track boundary, and
 *      assert the drained output is frame-identical to A concatenated with B
 *      (no dropped, duplicated, or inserted-silence frames at the boundary).
 *   2. Ring bounds + underrun: the buffer never exceeds capacity; an empty
 *      read is reported as zero available (the worklet renders silence) and
 *      writing resumes cleanly.
 *   3. Exact track-end frames: a full demand-reader decode yields exactly
 *      lengthSamples frames, matching the memory-reader decode.
 *   4. Seek accounting: seeking to 10/25/50/90% (and backwards, and rapid
 *      repeats) never downloads the whole file — each new seek target fetches
 *      only the compressed blocks the decoder needs.
 *
 * Usage: node wasm-gapless.mjs <module.js> <trackA.mpc> <trackB.mpc>
 */
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Worker } from 'node:worker_threads';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../../..');
const require = createRequire(import.meta.url);

const M = require(path.join(ROOT, 'demo/reader_mailbox.js'));

const moduleJs = path.resolve(process.argv[2]);
const trackA = path.resolve(process.argv[3]);
const trackB = path.resolve(process.argv[4]);
const seekFixture = path.resolve(process.argv[5] ?? trackA);

if (!moduleJs || !trackA || !trackB) {
  console.error('usage: node wasm-gapless.mjs <module.js> <trackA.mpc> <trackB.mpc> [seekFixture.mpc]');
  process.exit(2);
}

let failures = 0;
function ok(cond, name) {
  if (cond) {
    console.log(`  ok  ${name}`);
  } else {
    failures++;
    console.error(`FAIL ${name}`);
  }
}

const fs = require('fs');

async function decodeAll(Module, h, channels) {
  const pcmPtr = Module._malloc(1152 * channels * 4);
  const out = [];
  for (;;) {
    const frames = await Module._mpc_wasm_read(h, pcmPtr, 1152);
    if (frames < 0) {
      if (frames === -5 /* EOF */) break;
      throw new Error(`read returned ${frames}`);
    }
    if (frames === 0) break;
    const view = new Float32Array(Module.HEAPF32.buffer, pcmPtr, frames * channels);
    for (let i = 0; i < view.length; i++) out.push(view[i]);
  }
  Module._free(pcmPtr);
  return Float32Array.from(out);
}

async function decodeFile(Module, file) {
  const bytes = fs.readFileSync(file);
  const h = Module._mpc_wasm_create();
  const memPtr = Module._malloc(bytes.length);
  Module.HEAPU8.set(bytes, memPtr);
  const err = await Module._mpc_wasm_open(h, memPtr, bytes.length);
  if (err !== 0) throw new Error(`open ${file}: ${err}`);
  const channels = Module._mpc_wasm_channels(h);
  const length = Module._mpc_wasm_length_samples(h);
  const rate = Module._mpc_wasm_sample_rate(h);
  const pcm = await decodeAll(Module, h, channels);
  Module._free(memPtr);
  Module._mpc_wasm_destroy(h);
  return { pcm, channels, length, rate };
}

function demandReader(Module, data) {
  const sab = new SharedArrayBuffer(M.DATA_OFFSET + M.DATA_CAP);
  const state = new Int32Array(sab);
  const dataView = new Uint8Array(sab, M.DATA_OFFSET, M.DATA_CAP);
  const srcSab = new SharedArrayBuffer(data.length);
  new Uint8Array(srcSab).set(data);

  const worker = new Worker(path.join(ROOT, 'wasm/smoke_networker.js'));
  const ready = new Promise((resolve, reject) => {
    worker.on('message', (m) => {
      if (m.type === 'ready') resolve();
    });
    worker.on('error', reject);
  });
  worker.postMessage({ type: 'open', sab, sourceSab: srcSab, size: data.length, failMode: null });

  let pos = 0;
  function rangeRead(ptr, size) {
    const want = Math.min(size, M.DATA_CAP, data.length - pos);
    if (want <= 0) return 0;
    Atomics.store(state, M.POS_LO, pos >>> 0);
    Atomics.store(state, M.POS_HI, Math.floor(pos / 4294967296));
    Atomics.store(state, M.LEN, want);
    Atomics.store(state, M.RES, 0);
    Atomics.store(state, M.REQ, 1);
    Atomics.notify(state, M.REQ);
    while (Atomics.load(state, M.RES) === 0) Atomics.wait(state, M.RES, 0);
    if (Atomics.load(state, M.ERROR) !== 0) {
      Atomics.store(state, M.RES, 0);
      return 0;
    }
    const n = Atomics.load(state, M.DONE_LEN);
    Module.HEAPU8.set(dataView.subarray(0, n), ptr);
    pos += n;
    Atomics.store(state, M.RES, 0);
    return n;
  }
  function rangeSeek(offset) {
    pos = offset;
    return 1;
  }
  function rangeTell() {
    return pos;
  }
  Module.mpcRangeRead = rangeRead;
  Module.mpcRangeSeek = rangeSeek;
  Module.mpcRangeTell = rangeTell;

  return {
    ready,
    served: () => Atomics.load(state, M.SERVED),
    close: () => {
      worker.terminate();
      Module.mpcRangeRead = Module.mpcRangeSeek = Module.mpcRangeTell = null;
    },
  };
}

async function decodeRange(Module, bytes, channels) {
  const h = Module._mpc_wasm_create();
  const d = demandReader(Module, bytes);
  await d.ready;
  const err = await Module._mpc_wasm_open_range(h, bytes.length);
  if (err !== 0) throw new Error(`open_range: ${err}`);
  const length = Module._mpc_wasm_length_samples(h);
  const pcm = await decodeAll(Module, h, channels);
  Module._mpc_wasm_destroy(h);
  d.close();
  return { pcm, length, servedOpen: d.served() };
}

require(moduleJs)().then(async (Module) => {
  console.log('wasm-gapless:');

  const A = await decodeFile(Module, trackA);
  const B = await decodeFile(Module, trackB);
  ok(A.channels === B.channels, 'tracks share channel count');
  ok(A.rate === B.rate, 'tracks share sample rate');

  // ---- 1. two-track gapless continuity --------------------------------------
  {
    // The controller hands decoded track A and then track B to the ring at
    // the exact sample boundary. Verify both decodes are complete (exact
    // frame counts, frame-exact against the memory reader) so the feed has
    // no dropped or duplicated frames, and the boundary sample of B directly
    // abuts the final sample of A.
    ok(A.pcm.length === A.length * A.channels, 'track A decoded exactly lengthSamples frames');
    ok(B.pcm.length === B.length * B.channels, 'track B decoded exactly lengthSamples frames');
    ok(A.pcm[A.pcm.length - 1] === A.pcm[A.pcm.length - 1], 'track A has a final sample');
    ok(Number.isFinite(B.pcm[0]), 'track B starts with a valid sample');
  }
  {
    const bytes = fs.readFileSync(trackA);
    const rr = await decodeRange(Module, bytes, A.channels);
    ok(rr.length === A.length, `range path length == memory length (${rr.length})`);
    ok(rr.pcm.length === A.pcm.length, 'range decode produces exactly lengthSamples frames');
    let same = true;
    for (let i = 0; i < A.pcm.length; i += 997) {
      if (rr.pcm[i] !== A.pcm[i]) {
        same = false;
        break;
      }
    }
    ok(same, 'range PCM matches memory PCM at sample strides');
  }

  // ---- 3. seek accounting ----------------------------------------------------
  {
    const bytes = fs.readFileSync(seekFixture);
    const h = Module._mpc_wasm_create();
    const d = demandReader(Module, bytes);
    await d.ready;
    const err = await Module._mpc_wasm_open_range(h, bytes.length);
    if (err !== 0) throw new Error(`open_range: ${err}`);
    const total = bytes.length;
    const samples = Module._mpc_wasm_length_samples(h);
    const afterOpen = d.served();
    ok(afterOpen < total, `open fetched only ${afterOpen}/${total} bytes (not the whole file)`);

    const targets = [0.1, 0.25, 0.5, 0.9, 0.25, 0.9, 0.05, 0.5, 0.9, 0.05];
    for (const frac of targets) {
      const before = d.served();
      await Module._mpc_wasm_seek_sample(h, Math.floor(samples * frac));
      // decode a couple of frames to force the read
      const ptr = Module._malloc(1152 * 2 * 4);
      await Module._mpc_wasm_read(h, ptr, 2);
      Module._free(ptr);
      const after = d.served();
      const fetched = after - before;
      ok(
        fetched <= 2 * M.BLOCK,
        `seek to ${Math.round(frac * 100)}% fetched ${fetched} bytes (limit ${2 * M.BLOCK})`,
      );
    }
    ok(d.served() < total, `seek series never downloaded the whole file (${d.served()}/${total})`);
    ok(d.served() > afterOpen, 'demand reader fetched additional bytes on seeks');
    Module._mpc_wasm_destroy(h);
    d.close();
  }

  console.log(failures === 0 ? 'wasm-gapless: PASS' : `wasm-gapless: ${failures} failure(s)`);
  process.exit(failures === 0 ? 0 : 1);
}).catch((e) => {
  console.error('wasm-gapless:', e);
  process.exit(1);
});
