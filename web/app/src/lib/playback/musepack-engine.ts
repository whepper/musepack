// The Musepack playback engine: owns the AudioContext, the PCM AudioWorklet
// (with its bounded ring), and TWO decoder workers so the next track is
// already opened when the current one ends (gapless). The playback controller
// drives it; the UI never touches it directly.
// The PCM AudioWorklet. In dev Vite serves the TS source transformed; in the
// production build it is bundled to /assets/worklet.js (see vite.config.ts).
const WORKLET_URL = import.meta.env.PROD
  ? '/assets/worklet.js'
  : '/src/lib/playback/audio-worklet.ts';
import type { WorkletReport } from './worklet-protocol';

export interface EngineStreamInfo {
  rate: number;
  channels: number;
  version: number;
  lengthSamples: number;
}

export interface EngineEvents {
  onPrimed: () => void;
  onBuffering: () => void;
  onEos: () => void; // current track finished decoding
  onEnded?: () => void; // stream drained and playback reached the end
  onError: (message: string) => void;
  onPosition: (streamSamples: number) => void;
}

interface WorkerHandle {
  worker: Worker;
  info: EngineStreamInfo | null;
  eos: boolean;
  nextUrl: string | null;
}

const DECODER_WORKER_URL = '/decoder.worker.js';

export class MusepackEngine {
  readonly kind = 'musepack' as const;
  private ctx: AudioContext | null = null;
  private gain: GainNode | null = null;
  private node: AudioWorkletNode | null = null;
  private current: WorkerHandle | null = null;
  private standby: WorkerHandle | null = null;
  private events: EngineEvents;
  private streamSamples = 0;
  /** Stream position at the last ring reset (seek/open); rendered is added. */
  private resetBase = 0;
  private token: string | null = null;
  private servedBytes = 0;
  private ready: Promise<void> | null = null;

  constructor(events: EngineEvents) {
    this.events = events;
  }

  /** Lazily creates the AudioContext + worklet. Must run after a user gesture. */
  private async ensureContext(): Promise<void> {
    if (this.ctx) return;
    this.ctx = new AudioContext();
    this.gain = this.ctx.createGain();
    this.gain.connect(this.ctx.destination);
    await this.ctx.audioWorklet.addModule(WORKLET_URL);
    this.node = new AudioWorkletNode(this.ctx, 'musicpack-pcm');
    this.node.connect(this.gain);
    this.node.port.onmessage = (ev: MessageEvent<WorkletReport>) => {
      this.onWorkletMessage(ev.data);
    };
  }

  async init(token: string | null): Promise<void> {
    this.token = token;
    await this.ensureContext();
  }

  private makeWorker(): WorkerHandle {
    const worker = new Worker(DECODER_WORKER_URL, { type: 'classic' });
    const h: WorkerHandle = { worker, info: null, eos: false, nextUrl: null };
    worker.onmessage = (ev: MessageEvent) => {
      this.onWorkerMessage(h, ev.data);
    };
    worker.onerror = (ev: ErrorEvent) => {
      this.events.onError('Decoder worker failed.');
      void ev;
    };
    return h;
  }

  private onWorkerMessage(h: WorkerHandle, msg: Record<string, unknown>): void {
    switch (msg.type) {
      case 'info':
        h.info = {
          rate: msg.rate as number,
          channels: msg.channels as number,
          version: msg.version as number,
          lengthSamples: msg.lengthSamples as number,
        };
        break;
      case 'pcm':
        if (h === this.current) {
          const samples = msg.samples as Float32Array;
          this.node?.port.postMessage(
            { type: 'samples', buffer: samples.buffer },
            [samples.buffer as ArrayBuffer],
          );
        }
        break;
      case 'seeked':
        break;
      case 'stats':
        this.servedBytes = msg.served as number;
        break;
      case 'eos':
        h.eos = true;
        if (h === this.current) this.events.onEos();
        break;
      case 'error':
        this.events.onError((msg.message as string) ?? 'Decoder error.');
        break;
    }
  }

  private onWorkletMessage(msg: WorkletReport): void {
    switch (msg.type) {
      case 'rendered':
        this.streamSamples = this.resetBase + msg.frames;
        this.events.onPosition(this.streamSamples);
        break;
      case 'primed':
        this.events.onPrimed();
        break;
      case 'need':
        this.startPumping();
        break;
      case 'full':
        this.pausePumping();
        break;
      case 'underrun':
        this.events.onBuffering();
        this.startPumping();
        break;
    }
  }

  /** Opens the first track in the current slot (fetches header + seek table). */
  async open(url: string, size: number): Promise<EngineStreamInfo> {
    await this.ensureContext();
    this.closeCurrent();
    const h = this.makeWorker();
    h.nextUrl = url;
    this.current = h;
    this.resetBase = 0;
    this.streamSamples = 0;
    const info = await this.openInWorker(h, url, size);
    this.configureWorklet(info);
    return info;
  }

  /** Opens the next track in the standby slot, ahead of the current one. */
  async prepareNext(url: string, size: number): Promise<EngineStreamInfo | null> {
    this.closeStandby();
    const h = this.makeWorker();
    h.nextUrl = url;
    this.standby = h;
    try {
      const info = await this.openInWorker(h, url, size);
      return info;
    } catch {
      this.closeStandby();
      return null;
    }
  }

  private async openInWorker(h: WorkerHandle, url: string, size: number): Promise<EngineStreamInfo> {
    const infoPromise = new Promise<EngineStreamInfo>((resolve, reject) => {
      const orig = h.worker.onmessage;
      h.worker.onmessage = (ev: MessageEvent) => {
        const msg = ev.data;
        if (msg.type === 'info') {
          h.info = {
            rate: msg.rate,
            channels: msg.channels,
            version: msg.version,
            lengthSamples: msg.lengthSamples,
          };
          h.worker.onmessage = orig;
          resolve(h.info);
        } else if (msg.type === 'error') {
          reject(new Error(msg.message));
        }
      };
    });
    h.worker.postMessage({ type: 'open', url, size, token: this.token });
    return infoPromise;
  }

  private configureWorklet(info: EngineStreamInfo): void {
    this.node?.port.postMessage({
      type: 'config',
      rate: info.rate,
      channels: info.channels,
    });
    this.node?.port.postMessage({ type: 'reset' });
    this.streamSamples = 0;
  }

  /** Promotes the standby worker to current (gapless handoff at EOS). */
  async advance(): Promise<EngineStreamInfo | null> {
    if (!this.standby || !this.standby.info) return null;
    const promoted = this.standby;
    this.standby = null;
    this.closeCurrent();
    this.current = promoted;
    return promoted.info;
  }

  startPumping(): void {
    this.current?.worker.postMessage({ type: 'play' });
  }

  pausePumping(): void {
    this.current?.worker.postMessage({ type: 'pause' });
  }

  /** Starts the audio clock (call after a user gesture or after priming). */
  async play(): Promise<void> {
    if (this.ctx && this.ctx.state !== 'running') await this.ctx.resume();
  }

  async pause(): Promise<void> {
    if (this.ctx && this.ctx.state === 'running') await this.ctx.suspend();
  }

  async seek(sample: number): Promise<void> {
    if (!this.current) return;
    this.node?.port.postMessage({ type: 'reset' });
    this.resetBase = sample;
    this.streamSamples = sample;
    await this.postSeek(sample);
    this.startPumping();
  }

  private postSeek(sample: number): Promise<void> {
    return new Promise((resolve) => {
      const h = this.current;
      if (!h) return resolve();
      const orig = h.worker.onmessage;
      h.worker.onmessage = (ev: MessageEvent) => {
        const msg = ev.data;
        h.worker.onmessage = orig;
        if (msg.type === 'seeked') {
          resolve();
        } else {
          this.onWorkerMessage(h, msg);
        }
      };
      h.worker.postMessage({ type: 'seek', sample });
    });
  }

  setGain(linear: number): void {
    if (this.gain) this.gain.gain.value = linear;
  }

  getPositionSamples(): number {
    return this.streamSamples;
  }

  /** Raw frames rendered since the last ring reset (seek/open). */
  getRenderedSamples(): number {
    return this.streamSamples - this.resetBase;
  }

  getServedBytes(): number {
    return this.servedBytes;
  }

  getInfo(): EngineStreamInfo | null {
    return this.current?.info ?? null;
  }

  get lengthSamples(): number {
    return this.current?.info?.lengthSamples ?? 0;
  }

  get rate(): number {
    return this.current?.info?.rate ?? 44100;
  }

  private closeCurrent(): void {
    if (this.current) {
      this.current.worker.postMessage({ type: 'close' });
      this.current.worker.terminate();
      this.current = null;
    }
  }

  private closeStandby(): void {
    if (this.standby) {
      this.standby.worker.postMessage({ type: 'close' });
      this.standby.worker.terminate();
      this.standby = null;
    }
  }

  async close(): Promise<void> {
    this.closeCurrent();
    this.closeStandby();
    if (this.ctx) {
      await this.ctx.close();
      this.ctx = null;
      this.gain = null;
      this.node = null;
    }
  }
}
