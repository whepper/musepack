import { writable } from '../store';
import type { AlbumLoudness, ReleaseDetail, Track } from '../api/types';

export interface QueueItem {
  track: Track;
  releaseId: number;
  albumId: number;
  albumTitle: string;
  artist: string;
  edition?: string;
  albumLoudness?: AlbumLoudness;
  artworkUrl?: string;
}

export interface QueueState {
  items: QueueItem[];
  /** Index of the current item (-1 when the queue is empty). */
  index: number;
}

function empty(): QueueState {
  return { items: [], index: -1 };
}

/** Builds the play sequence for a release in media/disc-major, track order. */
export function tracksOfRelease(release: ReleaseDetail): Track[] {
  const out: Track[] = [];
  for (const disc of [...release.media].sort((a, b) => a.disc - b.disc)) {
    for (const track of [...disc.tracks].sort((a, b) => a.number - b.number)) {
      out.push(track);
    }
  }
  return out;
}

export function createQueueStore() {
  const store = writable<QueueState>(empty());

  function itemsFor(release: ReleaseDetail, title: string, artist: string): QueueItem[] {
    const artworkUrl = release.artwork[0]?.url;
    return tracksOfRelease(release).map((track) => ({
      track,
      releaseId: release.id,
      albumId: release.album.id,
      albumTitle: title,
      artist,
      edition: release.edition,
      albumLoudness: release.loudness,
      artworkUrl,
    }));
  }

  return {
    ...store,
    state: store.get,
    /** Play the given release from `startIndex` (default first track). */
    playAlbum(release: ReleaseDetail, title: string, artist: string, startIndex = 0): QueueItem {
      const items = itemsFor(release, title, artist);
      if (items.length === 0) throw new Error('This release has no playable tracks.');
      const index = Math.max(0, Math.min(startIndex, items.length - 1));
      store.set({ items, index });
      const item = items[index] ?? items[0];
      if (!item) throw new Error('This release has no playable tracks.');
      return item;
    },
    /** Replace the queue with a single item. */
    playNow(item: QueueItem): QueueItem {
      store.set({ items: [item], index: 0 });
      return item;
    },
    /** Insert right after the current item. */
    playNext(item: QueueItem): void {
      store.update((s) => {
        if (s.items.length === 0) return { items: [item], index: 0 };
        const at = s.index + 1;
        const items = [...s.items];
        items.splice(at, 0, item);
        return { items, index: s.index };
      });
    },
    /** Append to the end of the queue. */
    enqueue(item: QueueItem): void {
      store.update((s) => ({ items: [...s.items, item], index: s.index }));
    },
    enqueueMany(items: QueueItem[]): void {
      store.update((s) => ({ items: [...s.items, ...items], index: s.index }));
    },
    current(): QueueItem | null {
      const s = store.get();
      return s.index >= 0 && s.index < s.items.length ? s.items[s.index] ?? null : null;
    },
    at(i: number): QueueItem | null {
      return store.get().items[i] ?? null;
    },
    next(): QueueItem | null {
      let item: QueueItem | null = null;
      store.update((s) => {
        if (s.index + 1 >= s.items.length) return s;
        const next = { ...s, index: s.index + 1 };
        item = next.items[next.index] ?? null;
        return next;
      });
      return item;
    },
    previous(): QueueItem | null {
      let item: QueueItem | null = null;
      store.update((s) => {
        if (s.index - 1 < 0) return s;
        const next = { ...s, index: s.index - 1 };
        item = next.items[next.index] ?? null;
        return next;
      });
      return item;
    },
    /** Move the cursor to an item (no queue rebuild). */
    moveTo(i: number): void {
      store.update((s) => {
        if (i < 0 || i >= s.items.length) return s;
        return { ...s, index: i };
      });
    },
    removeAt(i: number): void {
      store.update((s) => {
        if (i < 0 || i >= s.items.length) return s;
        const items = s.items.filter((_, idx) => idx !== i);
        let index = s.index;
        if (i < s.index) index = s.index - 1;
        else if (i === s.index) index = Math.min(index, items.length - 1);
        return { items, index };
      });
    },
    clear(): void {
      store.set(empty());
    },
  };
}

export type QueueStore = ReturnType<typeof createQueueStore>;
