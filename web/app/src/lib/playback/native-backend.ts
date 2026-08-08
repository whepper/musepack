// Native-codec backend: plays browser-supported formats (FLAC, Vorbis, WAV,
// ...) through HTMLMediaElement routed into the shared gain pipeline.
//
// Gapless: the next track is preloaded into a second element ahead of time
// and swapped in on 'ended'. Browsers do not expose a sample-perfect gapless
// API for <audio>, so the boundary may carry a small gap — unlike the
// Musepack backend, which is exact. This is documented in web/README.md.
import type { EngineStreamInfo } from './musepack-engine';

interface ElementSlot {
  el: HTMLAudioElement;
  src: MediaElementAudioSourceNode;
  url: string;
}

export class NativeBackend {
  readonly kind = 'native' as const;
  private ctx: AudioContext | null = null;
  private gain: GainNode | null = null;
  private current: ElementSlot | null = null;
  private standby: ElementSlot | null = null;
  rate = 44100;
  private channels = 2;
  private token: string | null = null;

  // events wired by the controller
  onEos: (() => void) | null = null;
  onError: ((msg: string) => void) | null = null;
  onPosition: (() => void) | null = null;
  onPrimed: (() => void) | null = null;
  onBuffering: (() => void) | null = null;

  async init(token: string | null): Promise<void> {
    this.token = token;
    this.ctx = new AudioContext();
    this.gain = this.ctx.createGain();
    this.gain.connect(this.ctx.destination);
  }

  private makeSlot(): ElementSlot {
    if (!this.ctx || !this.gain) throw new Error('native backend not initialized');
    const el = new Audio();
    el.crossOrigin = 'anonymous';
    el.preload = 'auto';
    const src = this.ctx.createMediaElementSource(el);
    src.connect(this.gain);
    el.addEventListener('ended', () => this.onEos?.());
    el.addEventListener('waiting', () => this.onBuffering?.());
    el.addEventListener('error', () => {
      this.onError?.('This format cannot be played in your browser.');
    });
    el.addEventListener('timeupdate', () => this.onPosition?.());
    return { el, src, url: '' };
  }

  private loadInto(slot: ElementSlot, url: string): void {
    slot.url = url;
    // Same-origin audio (session cookie authenticates). crossOrigin anonymous
    // keeps the response CORS-clean so createMediaElementSource can route it.
    slot.el.src = url;
    slot.el.load();
  }

  async open(url: string, size: number): Promise<EngineStreamInfo> {
    void size;
    const slot = this.makeSlot();
    this.disposeCurrent();
    this.current = slot;
    this.loadInto(slot, url);
    const info = await this.metadata(slot);
    this.rate = info.rate;
    return info;
  }

  async prepareNext(url: string, size: number): Promise<EngineStreamInfo | null> {
    void size;
    this.disposeStandby();
    const slot = this.makeSlot();
    this.standby = slot;
    this.loadInto(slot, url);
    try {
      return await this.metadata(slot);
    } catch {
      this.disposeStandby();
      return null;
    }
  }

  private metadata(slot: ElementSlot): Promise<EngineStreamInfo> {
    return new Promise((resolve, reject) => {
      if (slot.el.readyState >= 1 && slot.el.duration > 0 && !Number.isNaN(slot.el.duration)) {
        resolve(this.infoOf(slot));
        return;
      }
      const onMeta = () => {
        cleanup();
        resolve(this.infoOf(slot));
      };
      const onErr = () => {
        cleanup();
        reject(new Error('unplayable'));
      };
      const cleanup = () => {
        slot.el.removeEventListener('loadedmetadata', onMeta);
        slot.el.removeEventListener('error', onErr);
      };
      slot.el.addEventListener('loadedmetadata', onMeta);
      slot.el.addEventListener('error', onErr);
    });
  }

  private infoOf(slot: ElementSlot): EngineStreamInfo {
    const duration = slot.el.duration > 0 ? slot.el.duration : 0;
    return {
      rate: this.rate,
      channels: this.channels,
      version: 0,
      lengthSamples: Math.floor(duration * this.rate),
    };
  }

  async advance(): Promise<EngineStreamInfo | null> {
    if (!this.standby) return null;
    const promoted = this.standby;
    this.standby = null;
    this.disposeCurrent();
    this.current = promoted;
    try {
      await promoted.el.play();
    } catch {
      /* autoplay policy — the controller retries on the next gesture */
    }
    this.onPrimed?.();
    return this.infoOf(promoted);
  }

  startPumping(): void {
    if (this.current) void this.current.el.play();
  }

  pausePumping(): void {
    // Native decode is browser-managed; no pump to pause.
  }

  async play(): Promise<void> {
    if (this.ctx && this.ctx.state !== 'running') await this.ctx.resume();
    if (this.current) await this.current.el.play();
    this.onPrimed?.();
  }

  async pause(): Promise<void> {
    if (this.current) this.current.el.pause();
  }

  async seek(sample: number): Promise<void> {
    if (!this.current) return;
    this.current.el.currentTime = sample / this.rate;
    if (this.current.el.paused) {
      await this.current.el.play().catch(() => undefined);
    }
    this.onPrimed?.();
  }

  setGain(linear: number): void {
    if (this.gain) this.gain.gain.value = linear;
  }

  getPositionSamples(): number {
    const el = this.current?.el;
    if (!el || Number.isNaN(el.currentTime)) return 0;
    return Math.max(0, Math.floor(el.currentTime * this.rate));
  }

  getRenderedSamples(): number {
    return this.getPositionSamples();
  }

  getInfo(): EngineStreamInfo | null {
    return this.current ? this.infoOf(this.current) : null;
  }

  get lengthSamples(): number {
    const el = this.current?.el;
    if (!el || Number.isNaN(el.duration)) return 0;
    return Math.floor(el.duration * this.rate);
  }

  private disposeCurrent(): void {
    if (this.current) {
      this.current.el.pause();
      this.current.el.src = '';
      this.current = null;
    }
  }

  private disposeStandby(): void {
    if (this.standby) {
      this.standby.el.pause();
      this.standby.el.src = '';
      this.standby = null;
    }
  }

  async close(): Promise<void> {
    this.disposeCurrent();
    this.disposeStandby();
    if (this.ctx) {
      await this.ctx.close();
      this.ctx = null;
      this.gain = null;
    }
  }
}
