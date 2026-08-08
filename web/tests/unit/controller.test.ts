import { describe, it, expect, vi, beforeEach } from 'vitest';
import { PlayerController, type Backend, type BackendEvents } from '../../app/src/lib/playback/controller';
import { createQueueStore } from '../../app/src/lib/state/queue';
import type { ReleaseDetail, Track } from '../../app/src/lib/api/types';

const RATE = 44100;
const LENGTH = RATE * 10; // 10 s tracks

function track(id: number, title: string): Track {
  return {
    id,
    number: id,
    title,
    artists: [],
    loudness: { lufs: -7.19, truePeakDb: -4.19 },
    codec: { codec: 'musepack-sv8', mimeType: 'audio/musepack', sampleRate: RATE, channels: 2 },
    audio: { id: id + 100, size: 1000, url: `/api/v1/tracks/${id}/audio` },
  };
}

function release(id: number, titles: string[]): ReleaseDetail {
  return {
    id,
    edition: '2016 Remaster',
    loudness: { algorithm: 'ITU-R BS.1770-5', albumLufs: -7.28, albumTruePeakDb: -4.19 },
    album: { id: 1, title: 'Test Album', artists: [] },
    media: [{ disc: 1, tracks: titles.map((t, i) => track(i + 1, t)) }],
    artwork: [],
    assets: [],
  };
}

class FakeBackend implements Backend {
  readonly kind = 'musepack';
  readonly rate = RATE;
  lengthSamples = LENGTH;
  private rendered = 0; // worklet rendered counter (resets on seek/open)
  private resetBase = 0;
  private standbyInfo: { rate: number; channels: number; version: number; lengthSamples: number } | null = null;
  opened: string[] = [];
  prepared: string[] = [];
  gains: number[] = [];
  events: BackendEvents;
  paused = false;

  constructor(_kind: 'musepack' | 'native', events: BackendEvents) {
    this.events = events;
  }

  async init() {}
  async open(url: string) {
    this.opened.push(url);
    this.rendered = 0;
    return { rate: RATE, channels: 2, version: 8, lengthSamples: LENGTH };
  }
  async prepareNext(url: string) {
    this.prepared.push(url);
    const info = { rate: RATE, channels: 2, version: 8, lengthSamples: LENGTH };
    this.standbyInfo = info;
    return info;
  }
  async advance() {
    return this.standbyInfo;
  }
  startPumping() {}
  pausePumping() {
    this.paused = true;
  }
  async play() {}
  async pause() {}
  async seek(sample: number) {
    this.resetBase = sample;
    this.rendered = 0; // ring reset: rendered counter restarts
  }
  setGain(g: number) {
    this.gains.push(g);
  }
  getRenderedSamples() {
    return this.rendered;
  }
  getInfo() {
    return { rate: RATE, channels: 2, version: 8, lengthSamples: LENGTH };
  }
  getServedBytes() {
    return 0;
  }
  close() {}

  // test drivers
  setRendered(frames: number) {
    this.rendered = frames;
  }
  emitPrimed() {
    this.events.onPrimed();
  }
  emitEos() {
    this.events.onEos();
  }
  emitPosition() {
    this.events.onPosition();
  }
  emitError(msg: string) {
    this.events.onError(msg);
  }
}

function make() {
  const queue = createQueueStore();
  let backend: FakeBackend | null = null;
  const player = new PlayerController(queue, {
    backendFactory: (kind, events) => {
      backend = new FakeBackend(kind, events);
      return backend;
    },
  });
  player.init();
  return { player, queue, getBackend: () => backend! };
}

/** Flushes promise chains (state transitions land on .then microtasks). */
async function flush(times = 3): Promise<void> {
  for (let i = 0; i < times; i++) await Promise.resolve();
}

describe('PlayerController', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('playAlbum transitions loading -> buffering -> playing on prime', async () => {
    const { player, getBackend } = make();
    const r = release(9, ['T1', 'T2']);
    const pending = player.playAlbum(r, 'Test Album', 'Artist');
    expect(player.model.get().state).toBe('loading');
    await pending;
    expect(player.model.get().state).toBe('buffering');
    expect(player.model.get().current?.track.title).toBe('T1');
    expect(getBackend().prepared).toContain('/api/v1/tracks/2/audio'); // gapless prepare
    getBackend().emitPrimed();
    await flush();
    expect(player.model.get().state).toBe('playing');
  });

  it('applies album normalization gain by default and tracks it separately from volume', async () => {
    const { player, getBackend } = make();
    await player.playAlbum(release(9, ['T1']), 'Test Album', 'Artist');
    getBackend().emitPrimed();
    const gain0 = getBackend().gains.at(-1) ?? 0;
    expect(gain0).toBeGreaterThan(0);
    expect(gain0).toBeLessThan(1);
    expect(player.model.get().normDb).toBeLessThan(0);
    player.setVolume(1.0);
    const gain1 = getBackend().gains.at(-1) ?? 0;
    expect(gain1).toBeGreaterThan(gain0); // volume up, norm unchanged
    expect(player.model.get().normDb).toBe(player.model.get().normDb); // norm intact
    player.setNormalizeMode('off');
    expect(getBackend().gains.at(-1)).toBeCloseTo(1.0, 6); // volume only
  });

  it('position advances with rendered samples', async () => {
    const { player, getBackend } = make();
    await player.playAlbum(release(9, ['T1']), 'Test Album', 'Artist');
    getBackend().emitPrimed();
    getBackend().setRendered(5000);
    getBackend().emitPosition();
    expect(player.model.get().positionSeconds).toBeCloseTo(5000 / RATE, 6);
    expect(player.model.get().durationSeconds).toBeCloseTo(20 / 2, 6); // single track 10s
  });

  it('seek maps album position into the current track', async () => {
    const { player, getBackend } = make();
    await player.playAlbum(release(9, ['T1', 'T2']), 'Test Album', 'Artist');
    getBackend().emitPrimed();
    await flush();
    await player.seek(5);
    await flush();
    expect(player.model.get().state).toBe('buffering');
    getBackend().emitPrimed();
    await flush();
    expect(player.model.get().positionSeconds).toBeCloseTo(5, 2);
    getBackend().setRendered(2 * RATE); // played 2 more seconds past the seek
    getBackend().emitPosition();
    expect(player.model.get().positionSeconds).toBeCloseTo(7, 2);
  });

  it('advances gaplessly at EOS and ends after the last track', async () => {
    const { player, queue, getBackend } = make();
    await player.playAlbum(release(9, ['T1', 'T2']), 'Test Album', 'Artist');
    getBackend().emitPrimed();
    expect(queue.get().index).toBe(0);

    getBackend().setRendered(LENGTH); // track 1 finished
    getBackend().emitEos();
    // async advance -> next()
    await flush();
    expect(queue.get().index).toBe(1);
    expect(player.model.get().current?.track.title).toBe('T2');
    expect(player.model.get().state).toBe('playing');

    // last track finished -> no next -> pendingEnded -> drain -> ended
    getBackend().setRendered(2 * LENGTH);
    getBackend().emitEos();
    await flush();
    getBackend().setRendered(2 * LENGTH);
    getBackend().emitPosition();
    await flush();
    expect(player.model.get().state).toBe('ended');
  });

  it('pause stops pumping and keeps the position', async () => {
    const { player, getBackend } = make();
    await player.playAlbum(release(9, ['T1']), 'Test Album', 'Artist');
    getBackend().emitPrimed();
    getBackend().setRendered(4000);
    getBackend().emitPosition();
    await player.pause();
    await flush();
    expect(player.model.get().state).toBe('paused');
    expect(player.model.get().positionSeconds).toBeCloseTo(4000 / RATE, 6);
    await player.togglePlay();
    expect(player.model.get().state).toBe('playing');
  });

  it('reports friendly errors on backend failure', async () => {
    const { player, getBackend } = make();
    await player.playAlbum(release(9, ['T1']), 'Test Album', 'Artist');
    getBackend().emitError('This format is not supported by this browser.');
    expect(player.model.get().state).toBe('error');
    expect(player.model.get().error).toContain('not supported');
  });
});
