/*
 * AudioWorkletProcessor skeleton for the intended production playback path.
 *
 * The first proof-of-concept plays PCM on the main thread with
 * AudioBufferSourceNodes (see AudioBufferSink in main.js). The intended
 * architecture moves the PCM delivery into an AudioWorklet so playback is
 * sample-clock-accurate and the main thread only forwards {pcm} messages
 * from the decoder worker, which stays a pure decoder.
 *
 * This file is not active yet; it documents the target:
 *
 *   worker.js (decodes)  --{pcm}-->  main.js  --port.postMessage-->  this
 *   this.output fills the AudioWorklet output buffer each render quantum.
 *
 * Use it with:
 *   await audioCtx.audioWorklet.addModule('audio-worklet.js');
 *   const node = new AudioWorkletNode(audioCtx, 'musepack-pcm');
 *   node.connect(audioCtx.destination);
 */

class MusepackPcmProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.queue = [];       // interleaved Float32Array chunks
    this.pos = 0;          // sample index within this.chunk
    this.chunk = null;
    this.channels = 2;
    this.port.onmessage = (ev) => {
      if (ev.data.channels) this.channels = ev.data.channels;
      if (ev.data.samples) this.queue.push(ev.data.samples);
    };
  }

  process(inputs, outputs) {
    const output = outputs[0];
    const n = output[0].length;
    for (let i = 0; i < n; i++) {
      let value = 0;
      if (this.chunk && this.pos < this.chunk.length / this.channels) {
        value = this.chunk[this.pos * this.channels];
        this.pos++;
      } else if (this.queue.length > 0) {
        this.chunk = this.queue.shift();
        this.pos = 0;
        value = this.chunk[0];
        this.pos = 1;
      }
      for (let ch = 0; ch < output.length; ch++) {
        output[ch][i] = ch === 0 ? value : 0;
      }
    }
    return true;
  }
}

registerProcessor('musepack-pcm', MusepackPcmProcessor);
