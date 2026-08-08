// The unified playback controller: a single state machine over two backends
// (Musepack demand-driven WASM, and browser-native codecs), the queue, BS.1770
// normalization, and Media Session. The UI talks only to this controller.
import { writable, type Writable } from '../store';
import { MusepackEngine, type EngineStreamInfo } from './musepack-engine';
import { NativeBackend } from './native-backend';
import { combinedGain, normalizationGainDb, type NormalizationMode } from './loudness';
import { bindMediaActions, setMediaMetadata, setMediaPosition } from './media-session';
import type { QueueItem, QueueStore } from '../state/queue';
import type { Track } from '../api/types';

export type PlayerState =
  | 'idle'
  | 'loading'
  | 'buffering'
  | 'playing'
  | 'paused'
  | 'ended'
  | 'error';

export interface PlayerModel {
  state: PlayerState;
  current: QueueItem | null;
  positionSeconds: number;
  durationSeconds: number;
  volume: number;
  normalizeMode: NormalizationMode;
  normDb: number;
  error?: string;
}

export interface BackendEvents {
  onPrimed: () => void;
  onBuffering: () => void;
  onEos: () => void;
  onError: (message: string) => void;
  onPosition: () => void;
}

export interface Backend {
  readonly kind: 'musepack' | 'native';
  init(token: string | null): Promise<void>;
  open(url: string, size: number): Promise<EngineStreamInfo>;
  prepareNext(url: string, size: number): Promise<EngineStreamInfo | null>;
  advance(): Promise<EngineStreamInfo | null>;
  startPumping(): void;
  pausePumping(): void;
  play(): Promise<void>;
  pause(): Promise<void>;
  seek(sample: number): Promise<void>;
  setGain(linear: number): void;
  getRenderedSamples(): number;
  getInfo(): EngineStreamInfo | null;
  getServedBytes?(): number;
  readonly lengthSamples: number;
  readonly rate: number;
  close(): Promise<void>;
}

function createBackend(kind: 'musepack' | 'native', events: BackendEvents): Backend {
  if (kind === 'musepack') {
    return new MusepackEngine({
      onPrimed: events.onPrimed,
      onBuffering: events.onBuffering,
      onEos: events.onEos,
      onError: events.onError,
      onPosition: () => events.onPosition(),
    });
  }
  const native = new NativeBackend();
  native.onPrimed = events.onPrimed;
  native.onBuffering = events.onBuffering;
  native.onEos = events.onEos;
  native.onError = events.onError;
  native.onPosition = events.onPosition;
  return native;
}

export interface ControllerOptions {
  token?: () => string | null;
  initialVolume?: number;
  initialNormalize?: NormalizationMode;
  /** Test seam: replace backend construction. */
  backendFactory?: (kind: 'musepack' | 'native', events: BackendEvents) => Backend;
}

export class PlayerController {
  readonly model: Writable<PlayerModel>;
  private queue: QueueStore;
  private token: () => string | null;
  private backend: Backend | null = null;
  private backendKind: 'musepack' | 'native' | null = null;
  private lengths = new Map<number, number>();
  private resetOffset = 0;
  private pendingEnded = false;
  private loadingSeq = 0;
  private mutating = false;
  private backendFactory: ControllerOptions['backendFactory'];
  private unsub: (() => void) | null = null;

  constructor(queue: QueueStore, opts: ControllerOptions = {}) {
    this.queue = queue;
    this.token = opts.token ?? (() => null);
    this.backendFactory = opts.backendFactory;
    this.model = writable<PlayerModel>({
      state: 'idle',
      current: null,
      positionSeconds: 0,
      durationSeconds: 0,
      volume: opts.initialVolume ?? 0.8,
      normalizeMode: opts.initialNormalize ?? 'album',
      normDb: 0,
    });
  }

  init(): void {
    bindMediaActions({
      play: () => void this.togglePlay(),
      pause: () => void this.pause(),
      next: () => void this.next(),
      previous: () => void this.previous(),
      seek: (s) => this.seek(s),
      seekBy: (d) => this.seek(this.model.get().positionSeconds + d),
    });
    // React to queue changes made OUTSIDE the controller (e.g. the queue
    // panel removing the current item, or clearing the queue). Mutations the
    // controller itself performs are flagged `mutating` and skipped here.
    this.unsub = this.queue.subscribe(() => {
      if (this.mutating) return;
      const m = this.model.get();
      const cur = this.queue.current();
      if (!cur && m.state !== 'idle' && m.state !== 'error') {
        void this.stop();
      } else if (cur && m.current && cur.track.id !== m.current.track.id && m.state !== 'loading') {
        void this._load();
      }
    });
  }

  destroy(): void {
    this.unsub?.();
    this.unsub = null;
    void this.backend?.close();
  }

  // ---- state helpers ------------------------------------------------------

  private setState(state: PlayerState): void {
    this.model.update((m) => ({ ...m, state }));
  }

  private rate(): number {
    return this.backend?.rate ?? 44100;
  }

  private lengthOf(i: number): number {
    const exact = this.lengths.get(i);
    if (exact !== undefined) return exact;
    const item = this.queue.at(i);
    if (!item) return 0;
    return Math.floor((item.track.duration ?? 0) * this.rate());
  }

  private offsets(): number[] {
    const items = this.queue.get().items;
    const offs: number[] = [];
    let acc = 0;
    for (let i = 0; i < items.length; i++) {
      offs[i] = acc;
      acc += this.lengthOf(i);
    }
    return offs;
  }

  private totalLength(): number {
    const offs = this.offsets();
    const n = offs.length;
    if (n === 0) return 0;
    return (offs[n - 1] ?? 0) + this.lengthOf(n - 1);
  }

  private albumPosition(): number {
    if (!this.backend) return 0;
    const rendered = this.backend.getRenderedSamples();
    if (this.backendKind === 'musepack') return this.resetOffset + rendered;
    const idx = this.queue.get().index;
    const base = idx >= 0 ? (this.offsets()[idx] ?? 0) : 0;
    return base + rendered;
  }

  // ---- queue actions -------------------------------------------------------

  async playItem(item: QueueItem): Promise<void> {
    this.setState('loading');
    this.mutating = true;
    this.queue.playNow(item);
    this.mutating = false;
    await this._load();
  }

  async playAlbum(
    release: Parameters<QueueStore['playAlbum']>[0],
    title: string,
    artist: string,
    startIndex = 0,
  ): Promise<void> {
    this.setState('loading');
    this.mutating = true;
    this.queue.playAlbum(release, title, artist, startIndex);
    this.mutating = false;
    await this._load();
  }

  async next(): Promise<void> {
    this.mutating = true;
    const item = this.queue.next();
    this.mutating = false;
    if (item) await this._load();
  }

  async previous(): Promise<void> {
    if (this.model.get().positionSeconds > 3) {
      await this.seek(0);
      return;
    }
    this.mutating = true;
    const item = this.queue.previous();
    this.mutating = false;
    if (item) await this._load();
  }

  // ---- load / playback -----------------------------------------------------

  private async _load(): Promise<void> {
    const item = this.queue.current();
    if (!item) return;
    const seq = ++this.loadingSeq;
    this.pendingEnded = false;
    this.model.update((m) => ({ ...m, state: 'loading', error: undefined }));
    try {
      const kind = this.chooseBackend(item.track);
      await this.ensureBackend(kind);
      if (seq !== this.loadingSeq) return;
      const info = await this.backend!.open(item.track.audio.url, item.track.audio.size);
      if (seq !== this.loadingSeq) return;
      const qi = this.queue.get().index;
      this.lengths.set(qi, info.lengthSamples);
      // prepare the next track ahead of time (gapless)
      const next = this.queue.at(qi + 1);
      if (next) {
        const ni = await this.backend!.prepareNext(next.track.audio.url, next.track.audio.size);
        if (ni) this.lengths.set(qi + 1, ni.lengthSamples);
      }
      this.resetOffset = this.offsets()[qi] ?? 0;
      this.applyGain(item);
      this.setMediaFor(item);
      this.model.update((m) => ({
        ...m,
        current: item,
        state: 'buffering',
        positionSeconds: this.resetOffset / this.rate(),
        durationSeconds: this.totalLength() / this.rate(),
      }));
      await this.backend!.seek(0);
      this.backend!.startPumping();
    } catch (e) {
      if (seq === this.loadingSeq) this.fail(e instanceof Error ? e.message : String(e));
    }
  }

  private async onEos(): Promise<void> {
    if (!this.backend) return;
    const info = await this.backend.advance();
    this.mutating = true;
    const item = this.queue.next();
    this.mutating = false;
    if (!info || !item) {
      // Last track decoded; let the ring drain, then end.
      this.pendingEnded = true;
      this.backend.pausePumping();
      return;
    }
    const qi = this.queue.get().index;
    this.lengths.set(qi, info.lengthSamples);
    this.applyGain(item);
    this.setMediaFor(item);
    const next = this.queue.at(qi + 1);
    if (next) {
      const ni = await this.backend.prepareNext(next.track.audio.url, next.track.audio.size);
      if (ni) this.lengths.set(qi + 1, ni.lengthSamples);
    }
    this.model.update((m) => ({
      ...m,
      current: item,
      state: m.state === 'paused' ? 'paused' : 'playing',
      durationSeconds: this.totalLength() / this.rate(),
    }));
    this.backend.startPumping();
  }

  private onPrimed(): void {
    const m = this.model.get();
    if (m.state === 'loading' || m.state === 'buffering') {
      this.backend
        ?.play()
        .then(() => this.setState('playing'))
        .catch(() => undefined);
    }
  }

  private onBuffering(): void {
    this.setState('buffering');
  }

  private tick(): void {
    if (!this.backend) return;
    const pos = this.albumPosition();
    const dur = this.totalLength();
    const qi = this.queue.get().index;
    const idx = this.currentIndexAt(pos);
    if (idx !== qi && !this.pendingEnded) {
      this.mutating = true;
      this.queue.moveTo(idx);
      this.mutating = false;
    }
    const ended = this.pendingEnded && pos >= dur - 2;
    this.model.update((m) => ({
      ...m,
      positionSeconds: pos / this.rate(),
      durationSeconds: dur / this.rate(),
      state: ended ? 'ended' : m.state,
    }));
    setMediaPosition(dur / this.rate(), pos / this.rate());
    if (ended) {
      this.pendingEnded = false;
      this.backend.pause();
      this.backend.pausePumping();
    }
  }

  private currentIndexAt(pos: number): number {
    const offs = this.offsets();
    let idx = 0;
    for (let i = 0; i < offs.length; i++) {
      if (pos >= (offs[i] ?? 0)) idx = i;
    }
    return idx;
  }

  // ---- transport ------------------------------------------------------------

  async togglePlay(): Promise<void> {
    const m = this.model.get();
    if (m.state === 'playing') {
      await this.pause();
    } else if (m.state === 'paused') {
      await this.resume();
    } else if (m.state === 'ended' || m.state === 'idle' || m.state === 'error') {
      if (m.current) await this._load();
    } else {
      await this.resume();
    }
  }

  async pause(): Promise<void> {
    this.backend?.pausePumping();
    await this.backend?.pause();
    this.setState('paused');
  }

  async resume(): Promise<void> {
    if (!this.backend) return;
    this.backend.startPumping();
    await this.backend.play();
    this.setState('playing');
  }

  async seek(seconds: number): Promise<void> {
    if (!this.backend || !this.model.get().current) return;
    const posSamples = Math.max(0, Math.floor(seconds * this.rate()));
    const qi = this.queue.get().index;
    const base = this.offsets()[qi] ?? 0;
    const within = Math.max(0, posSamples - base);
    if (this.backendKind === 'musepack') this.resetOffset = posSamples;
    this.setState('buffering');
    this.model.update((m) => ({ ...m, positionSeconds: posSamples / this.rate() }));
    await this.backend.seek(within);
    this.backend.startPumping();
  }

  async stop(): Promise<void> {
    this.backend?.pausePumping();
    await this.backend?.pause();
    this.pendingEnded = false;
    this.model.update((m) => ({ ...m, state: 'idle', positionSeconds: 0 }));
  }

  // ---- settings --------------------------------------------------------------

  setVolume(v: number): void {
    const vol = Math.max(0, Math.min(1, v));
    this.model.update((m) => ({ ...m, volume: vol }));
    this.applyGain(this.model.get().current);
  }

  setNormalizeMode(mode: NormalizationMode): void {
    this.model.update((m) => ({ ...m, normalizeMode: mode }));
    this.applyGain(this.model.get().current);
  }

  private applyGain(item: QueueItem | null): void {
    const m = this.model.get();
    const normDb = normalizationGainDb(m.normalizeMode, item?.track.loudness, item?.albumLoudness);
    const gain = combinedGain(m.volume, normDb);
    this.backend?.setGain(gain);
    this.model.update((mm) => ({ ...mm, normDb }));
  }

  // ---- backend selection -----------------------------------------------------

  private chooseBackend(track: Track): 'musepack' | 'native' {
    const codec = track.codec?.codec ?? '';
    if (codec === 'musepack' || codec === 'musepack-sv7' || codec === 'musepack-sv8') {
      return 'musepack';
    }
    const mime = track.codec?.mimeType ?? '';
    if (mime && typeof document !== 'undefined') {
      const probe = document.createElement('audio');
      if (probe.canPlayType(mime)) return 'native';
    }
    throw new Error('This format is not supported by this browser.');
  }

  private async ensureBackend(kind: 'musepack' | 'native'): Promise<void> {
    if (this.backend && this.backendKind === kind) return;
    await this.backend?.close();
    this.backend = null;
    this.backendKind = kind;
    const events: BackendEvents = {
      onPrimed: () => this.onPrimed(),
      onBuffering: () => this.onBuffering(),
      onEos: () => void this.onEos(),
      onError: (msg) => this.fail(msg),
      onPosition: () => this.tick(),
    };
    this.backend = this.backendFactory
      ? this.backendFactory(kind, events)
      : createBackend(kind, events);
    await this.backend.init(this.token());
  }

  private setMediaFor(item: QueueItem): void {
    setMediaMetadata(item, item.artworkUrl);
  }

  private fail(msg: string): void {
    this.backend?.pausePumping();
    this.model.update((m) => ({ ...m, state: 'error', error: msg }));
  }

  getBackendKind(): 'musepack' | 'native' | null {
    return this.backendKind;
  }

  /** Compressed bytes fetched by the demand reader (dev/perf instrumentation). */
  getServedBytes(): number {
    if (this.backend && this.backendKind === 'musepack') {
      return this.backend.getServedBytes?.() ?? 0;
    }
    return 0;
  }
}
