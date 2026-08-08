// Message contract between the playback controller (main thread) and the
// MusicPack PCM AudioWorklet processor.

export const RING_SECONDS = 8;       // ring capacity
export const HIGH_WATER = 0.8;       // >= this fraction filled -> pause decode
export const LOW_WATER = 0.2;        // < this fraction -> resume decode
export const PRIME_FRACTION = 0.2;   // enough buffered before resuming the clock

export interface WorkletConfig {
  type: 'config';
  rate: number;
  channels: number;
}

export interface WorkletSamples {
  type: 'samples';
  buffer: ArrayBuffer; // interleaved Float32Array, transferred
}

export interface WorkletReset {
  type: 'reset';
}

// worklet -> main
export interface WorkletReport {
  type: 'rendered' | 'primed' | 'need' | 'full' | 'underrun';
  frames: number; // cumulative rendered frames (for 'rendered')
  available?: number;
}
