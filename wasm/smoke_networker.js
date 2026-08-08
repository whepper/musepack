/*
 * Node worker_thread for the demand-reader smoke test (wasm/smoke.js).
 *
 * Mirrors demo/networker.js: serves byte ranges from a SharedArrayBuffer
 * source through a block-aligned LRU cache, and can inject network failures
 * so the smoke test can verify seeking does not fetch the whole file and
 * that failures surface cleanly.
 */
"use strict";
const { parentPort } = require("worker_threads");
const M = require("../demo/reader_mailbox.js");

let state, data, source, size;
let failMode = null;
const cache = new Map(); // blockBase -> Uint8Array

function respond(ok, code, detail) {
  Atomics.store(state, M.ERROR, ok ? 0 : code);
  Atomics.store(state, M.ERROR2, detail || 0);
  Atomics.store(state, M.RES, 1);
  Atomics.notify(state, M.RES);
}

async function fakeFetchBlock(base) {
  const end = Math.min(size - 1, base + M.BLOCK - 1);
  const n = end - base + 1;

  if (failMode === "http401") { respond(false, M.ERR_HTTP, 401); throw new Error("401"); }
  if (failMode === "http503") { respond(false, M.ERR_HTTP, 503); throw new Error("503"); }
  if (failMode === "network") { respond(false, M.ERR_NETWORK, 0); throw new Error("network"); }
  if (failMode === "200") { respond(false, M.ERR_200, 200); throw new Error("200"); }
  if (failMode === "truncated") {
    respond(false, M.ERR_TRUNCATED, 0);
    throw new Error("truncated");
  }
  if (failMode === "badrange") {
    respond(false, M.ERR_RANGE, 1);
    throw new Error("bad content-range");
  }

  Atomics.add(state, M.SERVED, n);
  return source.subarray(base, end + 1);
}

async function handleRequest() {
  const pos = Atomics.load(state, M.POS_HI) * 4294967296 +
              (Atomics.load(state, M.POS_LO) >>> 0);
  const want = Math.min(Atomics.load(state, M.LEN), M.DATA_CAP, size - pos);
  if (want <= 0) {
    Atomics.store(state, M.DONE_LEN, 0);
    respond(true);
    return;
  }
  const base = Math.floor(pos / M.BLOCK) * M.BLOCK;
  let block = cache.get(base);
  if (!block) {
    if (cache.size >= M.MAX_BLOCKS) cache.delete(cache.keys().next().value);
    try {
      block = await fakeFetchBlock(base);
      cache.set(base, block);
    } catch (e) {
      return;
    }
  }
  const off = pos - base;
  const n = Math.min(want, block.length - off);
  data.set(block.subarray(off, off + n), 0);
  Atomics.store(state, M.DONE_LEN, n);
  respond(true);
}

async function runLoop() {
  for (;;) {
    while (Atomics.load(state, M.REQ) === 0)
      Atomics.wait(state, M.REQ, 0);
    Atomics.store(state, M.REQ, 0);
    try {
      await handleRequest();
    } catch (e) {
      if (Atomics.load(state, M.RES) === 0)
        respond(false, M.ERR_NETWORK, 0);
    }
  }
}

parentPort.on("message", (m) => {
  if (m.type !== "open") return;
  state = new Int32Array(m.sab);
  data = new Uint8Array(m.sab, M.DATA_OFFSET, M.DATA_CAP);
  source = new Uint8Array(m.sourceSab);
  size = m.size;
  failMode = m.failMode || null;
  Atomics.store(state, M.SERVED, 0);
  parentPort.postMessage({ type: "ready" });
  runLoop();
});
