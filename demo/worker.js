/*
 * Demo worker: owns the libmusepack WASM module and decodes PCM off the UI
 * thread. The message protocol here is deliberately a pure decode/PCM
 * streaming contract so the same {pcm} messages can later feed an
 * AudioWorklet (see audio-worklet.js) without changing the worker.
 *
 *   main -> worker  { type:'open', buffer } | { type:'openUrl', url, size }
 *                   { type:'play' } | { type:'pause' }
 *                   { type:'seek', sample } | { type:'close' }
 *   worker -> main  { type:'info', rate, channels, version, lengthSamples }
 *                   { type:'pcm', samples: Float32Array } | { type:'eos' }
 *                   { type:'error', message }
 */

importScripts('musepack.js');
importScripts('rangereader.js');

const FRAMES_PER_CHUNK = 8 * 1152; // 8 decoder frames per message

let Module = null;
let handle = -1;
let heapPtr = 0;
let pcmPtr = 0;
let channels = 2;
let playing = false;
let eos = false;
let pumping = false;
let rangeReader = null;

function post(msg) { self.postMessage(msg, msg.samples ? [msg.samples.buffer] : undefined); }

async function init() {
  if (Module) return;
  Module = await createMusepackModule();
}

async function open(buffer) {
  if (handle >= 0) destroy();
  handle = Module._mpc_wasm_create();
  if (handle < 0) throw new Error('mpc_wasm_create failed');

  heapPtr = Module._malloc(buffer.byteLength);
  Module.HEAPU8.set(new Uint8Array(buffer), heapPtr);

  const err = await Module._mpc_wasm_open(handle, heapPtr, buffer.byteLength);
  if (err !== 0) throw new Error('mpc_wasm_open returned ' + err);

  pcmPtr = Module._malloc(FRAMES_PER_CHUNK * channels * 4);
  postInfo();
}

/* Opens a track served by a musicpack-server over HTTP Range: the decoder's
   reads/seek go through the JS range imports, so playback and seeking
   exercise the server's Range handling end to end. */
async function openUrl(url, size) {
  if (handle >= 0) destroy();
  handle = Module._mpc_wasm_create();
  if (handle < 0) throw new Error('mpc_wasm_create failed');

  const reader = new MusicPackRange.RangeReader(url, size);
  rangeReader = await MusicPackRange.installRangeCallbacks(Module, reader);

  const err = await Module._mpc_wasm_open_range(handle, size);
  if (err !== 0) throw new Error('mpc_wasm_open_range returned ' + err);

  pcmPtr = Module._malloc(FRAMES_PER_CHUNK * channels * 4);
  postInfo();
}

function postInfo() {
  channels = Module._mpc_wasm_channels(handle);
  post({
    type: 'info',
    rate: Module._mpc_wasm_sample_rate(handle),
    channels,
    version: Module._mpc_wasm_stream_version(handle),
    lengthSamples: Module._mpc_wasm_length_samples(handle),
  });
}

function destroy() {
  if (handle >= 0) {
    if (pcmPtr) Module._free(pcmPtr);
    if (heapPtr) Module._free(heapPtr);
    Module._mpc_wasm_destroy(handle);
  }
  if (rangeReader) {
    Module.mpcRangeRead = Module.mpcRangeSeek = Module.mpcRangeTell = null;
    rangeReader = null;
  }
  handle = -1; heapPtr = 0; pcmPtr = 0;
}

async function readChunk() {
  const frames = await Module._mpc_wasm_read(handle, pcmPtr, FRAMES_PER_CHUNK);
  if (frames < 0) {
    if (frames === -5) { // MUSEPACK_ERR_EOF
      eos = true;
      post({ type: 'eos' });
    } else {
      post({ type: 'error', message: 'mpc_wasm_read returned ' + frames });
    }
    return;
  }
  if (frames === 0) {
    eos = true;
    post({ type: 'eos' });
    return;
  }
  const view = new Float32Array(Module.HEAPF32.buffer, pcmPtr, frames * channels);
  post({ type: 'pcm', samples: view.slice() });
}

async function pump() {
  if (!playing || eos || handle < 0) { pumping = false; return; }
  await readChunk();
  setTimeout(pump, 0); // keep the worker responsive
}

self.onmessage = async (ev) => {
  const msg = ev.data;
  try {
    await init();
    switch (msg.type) {
      case 'open':
        await open(msg.buffer);
        break;
      case 'openUrl':
        await openUrl(msg.url, msg.size);
        break;
      case 'play':
        playing = true;
        if (!pumping) { pumping = true; pump(); }
        break;
      case 'pause':
        playing = false;
        break;
      case 'seek':
        if (handle >= 0) {
          await Module._mpc_wasm_seek_sample(handle, msg.sample);
          eos = false;
          post({ type: 'seeked', sample: msg.sample });
        }
        break;
      case 'close':
        destroy();
        break;
    }
  } catch (e) {
    post({ type: 'error', message: String(e) });
  }
};
