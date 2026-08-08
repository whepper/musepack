// MusicPack PCM AudioWorkletProcessor (Phase 6).
//
// Consumes already-decoded interleaved PCM delivered as transferred
// Float32Array messages, buffers it in a bounded ring, and renders it to the
// audio output. Decoding and network never happen here. The ring fill is
// reported through watermarks so the main thread applies backpressure to the
// decoder (pause/resume) and never overflows the buffer.
//
// Bundled as a standalone entry by Vite (imported via `?url`); the ring
// buffer module is inlined at build time.
import { RingBuffer } from './ring-buffer';
import { HIGH_WATER, LOW_WATER, PRIME_FRACTION, RING_SECONDS } from './worklet-protocol';

class MusicPackPcmProcessor extends AudioWorkletProcessor {
  private ring: RingBuffer | null = null;
  private rate = 44100;
  private channels = 2;
  private lastRenderedReport = -1;
  private primed = false;
  private lastFull = true;
  private lastLow = true;

  constructor() {
    super();
    this.port.onmessage = (ev: MessageEvent) => {
      const msg = ev.data;
      switch (msg.type) {
        case 'config': {
          this.rate = msg.rate;
          this.channels = msg.channels;
          this.ring = new RingBuffer(Math.round(this.rate * RING_SECONDS), this.channels);
          this.primed = false;
          this.lastFull = true;
          this.lastLow = true;
          this.lastRenderedReport = -1;
          break;
        }
        case 'reset': {
          this.ring?.reset();
          this.primed = false;
          this.lastFull = true;
          this.lastLow = true;
          this.lastRenderedReport = -1;
          break;
        }
        case 'samples': {
          if (!this.ring) break;
          const data = new Float32Array(msg.buffer);
          this.ring.writeInterleaved(data);
          this.reportLevel();
          break;
        }
      }
    };
  }

  private reportLevel(): void {
    if (!this.ring) return;
    const fill = this.ring.availableFrames / this.ring.capacity;
    if (fill >= PRIME_FRACTION && !this.primed) {
      this.primed = true;
      this.port.postMessage({ type: 'primed', frames: this.ring.renderedFrames });
    }
    if (fill >= HIGH_WATER && !this.lastFull) {
      this.lastFull = true;
      this.port.postMessage({
        type: 'full',
        frames: this.ring.renderedFrames,
        available: this.ring.availableFrames,
      });
    } else if (fill < HIGH_WATER) {
      this.lastFull = false;
    }
    if (fill < LOW_WATER && !this.lastLow) {
      this.lastLow = true;
      this.port.postMessage({
        type: 'need',
        frames: this.ring.renderedFrames,
        available: this.ring.availableFrames,
      });
    } else if (fill >= LOW_WATER) {
      this.lastLow = false;
    }
  }

  private reportRendered(): void {
    if (!this.ring) return;
    const t = currentTime;
    if (this.lastRenderedReport < 0 || t - this.lastRenderedReport >= 0.2) {
      this.lastRenderedReport = t;
      this.port.postMessage({ type: 'rendered', frames: this.ring.renderedFrames });
    }
  }

  override process(inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
    const output = outputs[0];
    if (!output || output.length === 0) return true;
    const frames = output[0]?.length ?? 0;
    const ring = this.ring;

    if (!ring || ring.availableFrames < frames) {
      // Underrun: render silence and tell the controller we need data.
      for (const ch of output) ch.fill(0);
      if (ring) this.reportRendered();
      return true;
    }

    ring.readPlanar(output, frames);
    this.reportRendered();
    return true;
  }
}

registerProcessor('musicpack-pcm', MusicPackPcmProcessor);
