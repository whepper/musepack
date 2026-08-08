/*
 * Main-thread playback for the libmusepack WASM demo.
 *
 * PCM arrives from the decoder worker as {pcm} messages and is handed to a
 * PcmSink. The active sink (AudioBufferSink) schedules AudioBufferSourceNodes
 * sequentially — enough to prove browser playback of a decoded .mpc. The
 * AudioWorkletSink stub shows the intended production path; the worker
 * protocol needs no changes to switch to it.
 */

const fileInput = document.getElementById('file');
const playBtn = document.getElementById('play');
const stopBtn = document.getElementById('stop');
const seekEl = document.getElementById('seek');
const timeEl = document.getElementById('time');
const infoEl = document.getElementById('info');
const statusEl = document.getElementById('status');
const serverUrlEl = document.getElementById('serverurl');
const loadServerBtn = document.getElementById('loadserver');
const trackSelect = document.getElementById('trackselect');
const serverStatusEl = document.getElementById('serverstatus');
const tokenEl = document.getElementById('servertoken');

const worker = new Worker('worker.js');

let ctx = null;
let sink = null;
let stream = null; // { rate, channels, lengthSamples }
let playing = false;

/* ------------------------------------------------------------------ */
/* PcmSink abstraction                                                */
/* ------------------------------------------------------------------ */

class AudioBufferSink {
  constructor(context, rate, channels) {
    this.context = context;
    this.rate = rate;
    this.channels = channels;
    this.sources = [];
    this.nextStart = 0;   // sample position of the next buffer to schedule
    this.baseSample = 0;  // sample position when playback started
    this.baseTime = 0;    // context.currentTime when playback started
  }

  playFrom(sample) {
    this.baseSample = sample;
    this.baseTime = this.context.currentTime;
    this.nextStart = sample;
    this.context.resume();
  }

  push(samples) {
    const buffer = this.context.createBuffer(this.channels, samples.length / this.channels, this.rate);
    for (let ch = 0; ch < this.channels; ch++) {
      const data = buffer.getChannelData(ch);
      for (let i = 0; i < data.length; i++) data[i] = samples[i * this.channels + ch];
    }
    const src = this.context.createBufferSource();
    src.buffer = buffer;
    src.connect(this.context.destination);
    const when = Math.max(this.context.currentTime + 0.01, this.baseTime + (this.nextStart - this.baseSample) / this.rate);
    src.start(when);
    this.nextStart += dataLength(buffer);
    this.sources.push(src);
  }

  position() {
    return Math.floor(this.baseSample + (this.context.currentTime - this.baseTime) * this.rate);
  }

  clear() {
    for (const s of this.sources) { try { s.stop(); } catch (_) {} }
    this.sources = [];
  }
}

/* Intended production path: same {pcm} messages forwarded to an
 * AudioWorkletProcessor. Not used by this first proof-of-concept. */
class AudioWorkletSink {
  constructor(context, rate, channels) { void context; void rate; void channels; }
  playFrom() {}
  push() {}
  position() { return 0; }
  clear() {}
}

/* ------------------------------------------------------------------ */
/* Worker message handling                                             */
/* ------------------------------------------------------------------ */

worker.onmessage = (ev) => {
  const msg = ev.data;
  switch (msg.type) {
    case 'info':
      stream = msg;
      infoEl.textContent =
        `stream version ${msg.version}\n` +
        `${msg.rate} Hz, ${msg.channels} channel(s)\n` +
        `${msg.lengthSamples} samples (${fmtTime(msg.lengthSamples / msg.rate)})\n`;
      seekEl.max = String(msg.lengthSamples - 1);
      statusEl.textContent = 'Loaded. Press Play.';
      enableButtons(true);
      break;
    case 'pcm':
      if (sink && playing) {
        sink.push(msg.samples);
        maybeMore();
      }
      break;
    case 'seeked':
      if (sink) sink.playFrom(msg.sample);
      break;
    case 'eos':
      playing = false;
      playBtn.textContent = 'Play';
      statusEl.textContent = 'End of stream.';
      break;
    case 'error':
      statusEl.textContent = 'Error: ' + msg.message;
      playing = false;
      break;
  }
};

/* ------------------------------------------------------------------ */
/* Controls                                                            */
/* ------------------------------------------------------------------ */

function fmtTime(seconds) {
  const s = Math.max(0, Math.floor(seconds));
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
}

function enableButtons(ok) {
  playBtn.disabled = !ok;
  stopBtn.disabled = !ok;
  seekEl.disabled = !ok;
}

function maybeMore() {
  if (!sink) return;
  // Keep a few seconds ahead of playback.
  const ahead = sink.nextStart - sink.position();
  const seconds = ahead / stream.rate;
  if (seconds < 2 && playing) worker.postMessage({ type: 'play' });
}

async function ensureContext() {
  if (!ctx) {
    ctx = new AudioContext();
    await ctx.resume();
  }
  if (!sink) {
    sink = new AudioBufferSink(ctx, stream.rate, stream.channels);
  }
}

fileInput.addEventListener('change', async () => {
  const file = fileInput.files[0];
  if (!file) return;
  statusEl.textContent = 'Loading ' + file.name + '...';
  enableButtons(false);
  infoEl.textContent = '';
  if (ctx) { await ctx.close(); ctx = null; sink = null; }
  playing = false;
  playBtn.textContent = 'Play';
  const buffer = await file.arrayBuffer();
  worker.postMessage({ type: 'open', buffer }, [buffer]);
});

/* ------------------------------------------------------------------ */
/* musicpack-server streaming (HTTP Range)                             */
/* ------------------------------------------------------------------ */

/* API token: held in memory only (never localStorage). Sent as Bearer on
   every request, including the range fetches the network worker performs. */
function authHeaders() {
  const t = tokenEl.value.trim();
  return t ? { Authorization: 'Bearer ' + t } : {};
}

async function json(url) {
  const res = await fetch(url, { headers: authHeaders() });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}

loadServerBtn.addEventListener('click', async () => {
  const base = serverUrlEl.value.replace(/\/+$/, '');
  if (!base) { serverStatusEl.textContent = 'enter a server URL'; return; }
  if (!tokenEl.value.trim()) {
    serverStatusEl.textContent = 'enter an API token';
    return;
  }
  serverStatusEl.textContent = 'loading albums...';
  trackSelect.innerHTML = '';
  trackSelect.disabled = true;
  try {
    const data = await json(`${base}/api/v1/albums?limit=200`);
    const tracks = [];
    for (const album of data.albums) {
      const detail = await json(`${base}/api/v1/albums/${album.id}`);
      for (const release of detail.releases) {
        const rel = await json(`${base}/api/v1/releases/${release.id}`);
        for (const media of rel.media)
          for (const track of media.tracks)
            tracks.push({
              label: `${album.title} — ${track.title}`,
              url: `${base}${track.audio.url}`,
              size: track.audio.size,
            });
      }
    }
    if (tracks.length === 0) throw new Error('no tracks on server');
    window.__serverTracks = tracks;
    for (const t of tracks) {
      const opt = document.createElement('option');
      opt.value = String(tracks.indexOf(t));
      opt.textContent = t.label;
      trackSelect.appendChild(opt);
    }
    trackSelect.disabled = false;
    serverStatusEl.textContent = `${tracks.length} track(s)`;
  } catch (e) {
    serverStatusEl.textContent = 'error: ' + e.message;
  }
});

trackSelect.addEventListener('change', async () => {
  const tracks = window.__serverTracks;
  if (!tracks || trackSelect.value === '') return;
  const t = tracks[Number(trackSelect.value)];
  statusEl.textContent = 'Opening over HTTP Range: ' + t.label + '...';
  enableButtons(false);
  infoEl.textContent = '';
  if (ctx) { await ctx.close(); ctx = null; sink = null; }
  playing = false;
  playBtn.textContent = 'Play';
  // The worker decodes through a demand-driven range reader: the WASM
  // decoder fetches only the compressed ranges it needs (SharedArrayBuffer
  // + Atomics + a network worker), so playback starts before the full file
  // and seeking fetches just the required ranges.
  worker.postMessage({ type: 'openUrl', url: t.url, size: t.size,
                       token: tokenEl.value.trim() });
  statusEl.textContent = 'Streaming ' + t.label;
});

playBtn.addEventListener('click', async () => {
  if (!stream) return;
  await ensureContext();
  if (!playing) {
    playing = true;
    playBtn.textContent = 'Pause';
    if (sink.position() >= stream.lengthSamples) {
      worker.postMessage({ type: 'seek', sample: 0 }); // replay from start
    } else {
      sink.playFrom(sink.position());
    }
    worker.postMessage({ type: 'play' });
    statusEl.textContent = 'Playing.';
  } else {
    playing = false;
    playBtn.textContent = 'Play';
    await ctx.suspend();
    statusEl.textContent = 'Paused.';
  }
});

stopBtn.addEventListener('click', async () => {
  if (ctx) await ctx.suspend();
  playing = false;
  playBtn.textContent = 'Play';
  sink.clear();
  seekEl.value = 0;
  worker.postMessage({ type: 'seek', sample: 0 });
  statusEl.textContent = 'Stopped.';
});

let dragging = false;
seekEl.addEventListener('input', () => { dragging = true; });
seekEl.addEventListener('change', () => {
  dragging = false;
  const sample = Number(seekEl.value);
  sink.clear();
  worker.postMessage({ type: 'seek', sample });
  statusEl.textContent = 'Seeking...';
});

setInterval(() => {
  if (!stream) return;
  let pos = 0;
  if (sink) pos = Math.min(sink.position(), stream.lengthSamples);
  seekEl.value = String(pos);
  timeEl.textContent = `${fmtTime(pos / stream.rate)} / ${fmtTime(stream.lengthSamples / stream.rate)}`;
}, 200);

/* dataLength: helper — AudioBuffer.length */
function dataLength(buffer) { return buffer.length; }
