/*
 * MusicPack web client decoder worker (Phase 6).
 *
 * Classic worker that owns one libmusepack WASM instance plus the Phase 5
 * demand-driven range reader (networker + SharedArrayBuffer mailbox). The
 * playback controller keeps TWO of these: the current track's worker and a
 * second worker already opened on the NEXT track, so gapless album playback
 * continues at the exact sample boundary.
 *
 * Protocol (main -> worker):
 *   { type:'open', url, size, token? }   open a track (fetch header only)
 *   { type:'play' }                      start/continue decoding
 *   { type:'pause' }                     stop decoding
 *   { type:'seek', sample }              seek; stale decode output is dropped
 *   { type:'close' }                     free the handle + reader
 * (worker -> main):
 *   { type:'info', rate, channels, version, lengthSamples }
 *   { type:'pcm', samples: Float32Array }   interleaved, transferred
 *   { type:'seeked', sample }
 *   { type:'eos' }
 *   { type:'error', message }
 */

importScripts('musepack.js');
importScripts('reader_mailbox.js');
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
let demandReader = null;
let generation = 0;

function post(msg, transfer) {
  self.postMessage(
    msg,
    transfer ? [msg.samples.buffer] : undefined,
  );
}

async function init() {
  if (Module) return;
  Module = await createMusepackModule();
}

async function open(url, size, token) {
  if (handle >= 0) destroy();
  handle = Module._mpc_wasm_create();
  if (handle < 0) throw new Error('mpc_wasm_create failed');

  demandReader = await MusicPackRange.installDemandReader(Module, url, size,
                                                          token);
  const err = Module._mpc_wasm_open_range(handle, size);
  if (err !== 0) {
    const le = demandReader.lastError();
    demandReader.close();
    demandReader = null;
    throw new Error('mpc_wasm_open_range returned ' + err +
                    (le ? ` (reader error ${le})` : ''));
  }
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
  postStats();
}

function postStats() {
  if (demandReader) post({ type: 'stats', served: demandReader.served() });
}

function destroy() {
  if (handle >= 0) {
    if (pcmPtr) Module._free(pcmPtr);
    if (heapPtr) Module._free(heapPtr);
    Module._mpc_wasm_destroy(handle);
  }
  if (demandReader) {
    demandReader.close();
    demandReader = null;
  }
  handle = -1; heapPtr = 0; pcmPtr = 0;
}

async function readChunk() {
  const gen = generation;
  const frames = await Module._mpc_wasm_read(handle, pcmPtr, FRAMES_PER_CHUNK);
  if (gen !== generation) return; /* a seek arrived; drop stale decode */
  if (frames < 0) {
    if (frames === -5) { /* MUSEPACK_ERR_EOF */
      eos = true;
      post({ type: 'eos' });
    } else {
      let msg = 'decoder error ' + frames;
      if (demandReader && demandReader.lastError())
        msg += ` (stream read error ${demandReader.lastError()})`;
      post({ type: 'error', message: msg });
    }
    return;
  }
  if (frames === 0) {
    eos = true;
    post({ type: 'eos' });
    return;
  }
  const view = new Float32Array(Module.HEAPF32.buffer, pcmPtr, frames * channels);
  post({ type: 'pcm', samples: view.slice() }, true);
}

async function pump() {
  if (!playing || eos || handle < 0) {
    pumping = false;
    return;
  }
  await readChunk();
  if (playing && !eos) setTimeout(pump, 0);
}

self.onmessage = async (ev) => {
  const msg = ev.data;
  try {
    await init();
    switch (msg.type) {
      case 'open':
        generation++;
        playing = false;
        eos = false;
        await open(msg.url, msg.size, msg.token || null);
        break;
      case 'play':
        playing = true;
        if (!pumping) {
          pumping = true;
          pump();
        }
        break;
      case 'pause':
        playing = false;
        break;
      case 'seek':
        if (handle >= 0) {
          generation++;
          await Module._mpc_wasm_seek_sample(handle, msg.sample);
          eos = false;
          post({ type: 'seeked', sample: msg.sample });
          postStats();
        }
        break;
      case 'close':
        playing = false;
        eos = false;
        pumping = false;
        destroy();
        break;
    }
  } catch (e) {
    post({ type: 'error', message: String(e) });
  }
};
