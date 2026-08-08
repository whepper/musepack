// Bounded interleaved PCM ring buffer shared by the AudioWorklet and the
// decode feed. Pure TS so it runs both in the bundled worklet and in Node
// unit tests. Tracks absolute read/write positions; the worklet consumes
// frames, the controller (via the worklet) fills it.

export class RingBuffer {
  private data: Float32Array;
  private readonly capacityFrames: number;
  private readAbs = 0;
  private writeAbs = 0;
  readonly channels: number;

  constructor(capacityFrames: number, channels: number) {
    if (capacityFrames <= 0 || channels <= 0) {
      throw new Error('ring buffer needs positive capacity and channels');
    }
    this.capacityFrames = capacityFrames;
    this.channels = channels;
    this.data = new Float32Array(capacityFrames * channels);
  }

  get capacity(): number {
    return this.capacityFrames;
  }

  /** Frames available to read. */
  get availableFrames(): number {
    return this.writeAbs - this.readAbs;
  }

  /** Frames free to write. */
  get freeFrames(): number {
    return this.capacityFrames - this.availableFrames;
  }

  /** Total frames consumed since the last reset (monotonic playhead). */
  get renderedFrames(): number {
    return this.readAbs;
  }

  /** Total frames written since the last reset. */
  get writtenFrames(): number {
    return this.writeAbs;
  }

  /** Copies interleaved samples into the ring; returns frames written. */
  writeInterleaved(chunk: Float32Array): number {
    const frames = Math.floor(chunk.length / this.channels);
    const n = Math.min(frames, this.freeFrames);
    if (n <= 0) return 0;
    const ch = this.channels;
    for (let i = 0; i < n; i++) {
      const w = (this.writeAbs + i) % this.capacityFrames;
      const src = i * ch;
      const dst = w * ch;
      for (let c = 0; c < ch; c++) this.data[dst + c] = chunk[src + c] ?? 0;
    }
    this.writeAbs += n;
    return n;
  }

  /** Reads up to maxFrames interleaved frames into out; returns frames read. */
  readInterleaved(out: Float32Array, maxFrames: number): number {
    const n = Math.min(maxFrames, this.availableFrames, Math.floor(out.length / this.channels));
    if (n <= 0) return 0;
    const ch = this.channels;
    for (let i = 0; i < n; i++) {
      const r = (this.readAbs + i) % this.capacityFrames;
      const src = r * ch;
      const dst = i * ch;
      for (let c = 0; c < ch; c++) out[dst + c] = this.data[src + c] ?? 0;
    }
    this.readAbs += n;
    return n;
  }

  /**
   * Reads `frames` frames directly into planar channel outputs (the
   * AudioWorklet layout). Requires `frames <= availableFrames`.
   */
  readPlanar(outputs: Float32Array[], frames: number): number {
    const n = Math.min(frames, this.availableFrames);
    if (n <= 0) return 0;
    const ch = this.channels;
    for (let i = 0; i < n; i++) {
      const r = (this.readAbs + i) % this.capacityFrames;
      const src = r * ch;
      for (let c = 0; c < ch; c++) {
        const o = outputs[c];
        if (o) o[i] = this.data[src + c] ?? 0;
      }
    }
    this.readAbs += n;
    return n;
  }

  /** Drops everything; playhead accounting restarts at zero. */
  reset(): void {
    this.readAbs = 0;
    this.writeAbs = 0;
  }
}
